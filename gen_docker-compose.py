#!/bin/python

from string import Template
import argparse

# 创建解析器
parser = argparse.ArgumentParser(description='generate docker-compose.yml')

parser.add_argument('--count_nodes', type=int, default=12,
                    help='count of datanode')
parser.add_argument('--expected_nn', type=int, default=6,
                    help='count of namenode')

# 解析命令行参数
args = parser.parse_args()

COUNT_NODES = args.count_nodes
EXPECTED_NN = args.expected_nn

assert (COUNT_NODES >= EXPECTED_NN)

compose_conf = """
--nodes=${IPS}
--nn_port=8000
--dn_port=8001
--data_dir=/spkdfs/data
--log_dir=/spkdfs/logs
--coredumps_dir=/spkdfs/coredumps
--expected_nn=${EXPECTED_NN}
"""

compose_header = """
services:
"""

compose_body = """
  node${INDEX}:
    image: spkdfs:latest
    hostname: node${INDEX}
    command:
      [ "--flagfile=/spkdfs/node.conf" ]
    volumes:
      - ./tmp/spkdfs_${INDEX}/data:/spkdfs/data
      - ./tmp/spkdfs_${INDEX}/coredumps:/spkdfs/coredumps
      - ./tmp/spkdfs_${INDEX}/logs:/spkdfs/logs
      - ./node.conf:/spkdfs/node.conf:ro
    networks:
      spkdfs_net:
        ipv4_address: ${IP}
"""

compose_tail = """
networks:
  spkdfs_net:
    driver: bridge
    ipam:
      config:
        - subnet: 192.168.88.0/24
          gateway: 192.168.88.1
"""

template_body = Template(compose_body)
template_conf = Template(compose_conf.strip())
IPS = []
for i in range(1, COUNT_NODES+1):
  IPS.append("192.168.88.1{:02}".format(i))

with open("node.conf", "w") as f:
  rendered_str = template_conf.substitute(
      IPS=",".join(IPS),
      EXPECTED_NN=EXPECTED_NN
  )
  f.write(rendered_str)

with open("docker-compose.yml", 'w') as f:
  f.write(compose_header.lstrip())
  for i in range(1, COUNT_NODES+1):
    rendered_str = template_body.substitute(
        INDEX=i, IP=IPS[i-1])
    f.write(rendered_str)
  f.write(compose_tail.lstrip())
template_conf
