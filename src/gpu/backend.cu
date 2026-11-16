#include "cudacraft/gpu.hpp"
#include "cudacraft/terrain_math.hpp"
#include <cuda_runtime.h>
#include <array>
#include <stdexcept>

namespace cudacraft::gpu {
namespace {
void check(cudaError_t result) {
    if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}
__global__ void diagnostic_kernel(unsigned* values, unsigned count) {
    const unsigned index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < count) values[index] = index * 17u + 3u;
}
__global__ void flat_kernel(unsigned* blocks, unsigned count) {
    const unsigned index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count) return;
    const int y = static_cast<int>(index / 256) - 64;
    blocks[index] = y == -64 ? 85u : y < 60 ? 1u : y < 63 ? 10u : y == 63 ? 9u : 0u;
}
__global__ void overworld_kernel(unsigned* blocks,std::uint64_t seed,int cx,int cz) {
    const unsigned column=blockIdx.x*blockDim.x+threadIdx.x;
    if(column>=256) return;
    const int x=cx*16+static_cast<int>(column&15),z=cz*16+static_cast<int>(column>>4);
    const auto surface=terrain_math::column(seed,x,z);
    for(int y=-64;y<320;++y) blocks[(y+64)*256+column]=terrain_math::block(seed,x,y,z,surface);
}
__global__ void climate_kernel(unsigned* blocks,std::uint64_t seed,int cx,int cz,const terrain_math::Column* columns) {
    const unsigned column=blockIdx.x*blockDim.x+threadIdx.x;
    if(column>=256) return;
    const int x=cx*16+static_cast<int>(column&15),z=cz*16+static_cast<int>(column>>4);
    const auto surface=columns[column];
    for(int y=-64;y<320;++y) blocks[(y+64)*256+column]=terrain_math::block(seed,x,y,z,surface);
}
struct Resources {
    unsigned* device = nullptr;
    unsigned* host = nullptr;
    cudaStream_t stream = nullptr;
    terrain_math::Column* columns = nullptr;
    ~Resources() {
        if (stream) cudaStreamSynchronize(stream);
        if (device) cudaFree(device);
        if (columns) cudaFree(columns);
        if (host) cudaFreeHost(host);
        if (stream) cudaStreamDestroy(stream);
    }
};
}
bool compiled() { return true; }
std::vector<std::uint32_t> climate_terrain(std::int64_t seed,int cx,int cz,std::span<const terrain_math::Column> columns) {
    if(columns.size()!=256) throw std::runtime_error("Expected 256 climate columns");
    Resources resources;
    constexpr unsigned count=16*384*16;
    constexpr auto bytes=count*sizeof(unsigned);
    check(cudaStreamCreateWithFlags(&resources.stream,cudaStreamNonBlocking));
    check(cudaMalloc(reinterpret_cast<void**>(&resources.device),bytes));
    check(cudaMallocHost(reinterpret_cast<void**>(&resources.host),bytes));
    check(cudaMalloc(reinterpret_cast<void**>(&resources.columns),columns.size_bytes()));
    check(cudaMemcpyAsync(resources.columns,columns.data(),columns.size_bytes(),cudaMemcpyHostToDevice,resources.stream));
    climate_kernel<<<4,64,0,resources.stream>>>(resources.device,static_cast<std::uint64_t>(seed),cx,cz,resources.columns);
    check(cudaGetLastError());
    check(cudaMemcpyAsync(resources.host,resources.device,bytes,cudaMemcpyDeviceToHost,resources.stream));
    check(cudaStreamSynchronize(resources.stream));
    return std::vector<std::uint32_t>(resources.host,resources.host+count);
}
std::vector<std::uint32_t> overworld_terrain(std::int64_t seed,int cx,int cz) {
    Resources resources;
    constexpr unsigned count=16*384*16;
    constexpr auto bytes=count*sizeof(unsigned);
    check(cudaStreamCreateWithFlags(&resources.stream,cudaStreamNonBlocking));
    check(cudaMalloc(reinterpret_cast<void**>(&resources.device),bytes));
    check(cudaMallocHost(reinterpret_cast<void**>(&resources.host),bytes));
    overworld_kernel<<<4,64,0,resources.stream>>>(resources.device,static_cast<std::uint64_t>(seed),cx,cz);
    check(cudaGetLastError());
    check(cudaMemcpyAsync(resources.host,resources.device,bytes,cudaMemcpyDeviceToHost,resources.stream));
    check(cudaStreamSynchronize(resources.stream));
    return std::vector<std::uint32_t>(resources.host,resources.host+count);
}
std::vector<std::uint32_t> flat_terrain() {
    Resources resources;
    constexpr unsigned count = 16 * 384 * 16;
    constexpr auto bytes = count * sizeof(unsigned);
    check(cudaStreamCreateWithFlags(&resources.stream, cudaStreamNonBlocking));
    check(cudaMalloc(reinterpret_cast<void**>(&resources.device), bytes));
    check(cudaMallocHost(reinterpret_cast<void**>(&resources.host), bytes));
    flat_kernel<<<(count+255)/256,256,0,resources.stream>>>(resources.device,count);
    check(cudaGetLastError());
    check(cudaMemcpyAsync(resources.host,resources.device,bytes,cudaMemcpyDeviceToHost,resources.stream));
    check(cudaStreamSynchronize(resources.stream));
    return std::vector<std::uint32_t>(resources.host,resources.host+count);
}
int self_test(std::string& detail) {
    int devices = 0;
    const auto result = cudaGetDeviceCount(&devices);
    if (result == cudaErrorNoDevice || (result == cudaSuccess && devices == 0)) {
        detail = "CUDA compiled, but no CUDA device is available.";
        return 77;
    }
    try {
        check(result);
        Resources resources;
        constexpr unsigned count = 1024;
        constexpr auto bytes = count * sizeof(unsigned);
        check(cudaStreamCreateWithFlags(&resources.stream, cudaStreamNonBlocking));
        check(cudaMalloc(reinterpret_cast<void**>(&resources.device), bytes));
        check(cudaMallocHost(reinterpret_cast<void**>(&resources.host), bytes));
        diagnostic_kernel<<<4, 256, 0, resources.stream>>>(resources.device, count);
        check(cudaGetLastError());
        check(cudaMemcpyAsync(resources.host, resources.device, bytes, cudaMemcpyDeviceToHost, resources.stream));
        check(cudaStreamSynchronize(resources.stream));
        for (unsigned i = 0; i < count; ++i) {
            if (resources.host[i] != i * 17u + 3u) throw std::runtime_error("CUDA diagnostic output mismatch");
        }
        cudaDeviceProp properties{};
        check(cudaGetDeviceProperties(&properties, 0));
        detail = std::string("CUDA kernel/stream/memory self-test passed on ") + properties.name;
        return 0;
    } catch (const std::exception& error) {
        detail = error.what();
        return 1;
    }
}
}
