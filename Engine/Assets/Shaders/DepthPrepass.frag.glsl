#version 450

layout(location = 0) flat in uint inMeshID;

layout(location = 0) out uint visibilityBuffer;

void main() {
  visibilityBuffer = inMeshID;
}

