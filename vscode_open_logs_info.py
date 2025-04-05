import os
import subprocess
import time

base_dir = 'tmp/'
#target_file_name = 'node.INFO'
subdirs = [os.path.join(base_dir, d) for d in os.listdir(base_dir) if os.path.isdir(os.path.join(base_dir, d))]
lastdirs = []
for item in subdirs:
    if item.find("10") != -1:
        lastdirs.append(item)
        continue
    if item.find("11") != -1:
        lastdirs.append(item)
        continue
    if item.find("12") != -1:
        lastdirs.append(item)
        continue

subdirs = list(filter(lambda item: item not in lastdirs, subdirs))
subdirs = sorted(subdirs)
lastdirs = sorted(lastdirs)
subdirs = subdirs + lastdirs

subdirs = [path + "/logs/node_main.INFO" for path in subdirs]
# print(subdirs)

for index,file in enumerate(subdirs):
    time.sleep(0.1)
    #print(file)
    real_path = os.path.realpath(file)
    if index == 0:
        subprocess.run(["code", "--new-window", real_path])
    else:
        subprocess.run(["code", real_path])