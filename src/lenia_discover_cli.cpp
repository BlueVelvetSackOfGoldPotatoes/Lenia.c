/**
 * Lenia Creature Discovery CLI.
 * Searches random parameter space for stable organisms,
 * classifies their behavior, and saves to a JSON database.
 */
#include "lenia_discovery.h"
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    lenia::DiscoveryConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--size" && i+1 < argc) config.board_size = std::atoi(argv[++i]);
        else if (arg == "--steps" && i+1 < argc) config.eval_steps = std::atoi(argv[++i]);
        else if (arg == "--candidates" && i+1 < argc) config.max_candidates = std::atoi(argv[++i]);
        else if (arg == "--output" && i+1 < argc) config.output_path = argv[++i];
        else if (arg == "--animals" && i+1 < argc) config.animals_path = argv[++i];
        else if (arg == "--help") {
            std::cout << "Lenia Creature Discovery\n\n"
                      << "  --size N         Board size (default: 128)\n"
                      << "  --steps N        Eval steps per candidate (default: 500)\n"
                      << "  --candidates N   Total candidates to try (default: 10000)\n"
                      << "  --output PATH    Output JSON path (default: discovered.json)\n"
                      << "  --animals PATH   Known animals for novelty comparison\n"
                      << std::endl;
            return 0;
        }
    }

    lenia::DiscoveryEngine engine(config);
    int found = engine.run();
    return found > 0 ? 0 : 1;
}
