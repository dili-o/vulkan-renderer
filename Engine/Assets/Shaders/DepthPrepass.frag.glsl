#version 450

layout(location = 0) flat in uint objectID;

layout(location = 0) out uint visibilityBuffer;

void main() {
  visibilityBuffer = objectID;
}

