
#include "Sandbox.hpp"

int main() {
  Helix::Sandbox app;
  app.init();
  app.run();
  app.shutdown();
}
