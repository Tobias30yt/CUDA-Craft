"""Create an isolated flat 0.7 client-smoke fixture; never touches player worlds."""
import json
import time
from pathlib import Path

root=Path(__file__).resolve().parents[1]
folder=root/'.tools'/('simulation-client-world-'+str(time.time_ns()))
folder.mkdir()
items=[[0,0,0] for _ in range(46)]
items[36]=[913,1,3];items[37]=[953,8,0];items[38]=[1013,1,0]
empty=[[0,0,0] for _ in range(9)]
edits=[[-4,64,0,86],[1,64,-2,6570],[2,64,-2,4970],[3,64,-2,4970],[4,64,-2,9279],[2,64,2,3169]]
for y in range(64,67):
    for x in range(5,8):
        for z in range(5,8):
            if y==66 or x!=6 or z!=6:edits.append([x,y,z,562])
simulation={'version':1,'tick':0,'time':6000,'water':[[-4,64,0,5]],'devices':[],
    'players':{'000102030405060708090a0b0c0d0e0f':{'position':[.5,64,.5],
        'health':17,'food':12,'saturation':0,'exhaustion':0,'mode':0,
        'items':items,'crafting':empty,'cursor':[0,0,0]}},
    'mobs':[{'id':1000,'type':100,'position':[2.5,64,3.5],'health':10,'born':0,'drop':[0,0,0]},
            {'id':1001,'type':150,'position':[6.5,64,6.5],'health':20,'born':0,'drop':[0,0,0]},
            {'id':1002,'type':71,'position':[-2.5,64,3.5],'health':10,'born':0,'drop':[983,2,0]}]}
(folder/'blocks.json').write_text(json.dumps({'format':2,'protocol':774,'generator':'cuda-craft-flat-v1','seed':0,'edits':edits,'simulation':simulation}))
(root/'.tools/simulation-client-world.path').write_text(str(folder))
print(folder)
