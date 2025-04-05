python3 -m grpc_tools.protoc -I./src \
    --python_out=python \
    --pyi_out=python \
    --grpc_python_out=python \
    src/client/daemon.proto \
    src/common/datanode.proto \
    src/common/common.proto \
    src/common/namenode.proto
