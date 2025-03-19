#pragma once

#include "Core/Engine.hpp"
#include "Game.hpp"

extern Helix::Game *create_game();

int main() {
  Helix::Game *game = std::move(create_game());
  Helix::Engine engine;
  engine.init(game);

  game->shutdown();
  engine.shutdown();

  free(game);
  return 0;
}
