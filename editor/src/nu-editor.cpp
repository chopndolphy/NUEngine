#include <nu-core.h>

int main() {
    nuEngine engine;

    engine.init();

    engine.run();

    engine.cleanup();

    return 0;
}
