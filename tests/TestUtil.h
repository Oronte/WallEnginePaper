#pragma once

// Mini-cadre de test (pas de dependance) : chaque fichier expose une fonction Run...Tests() appelee par
// TestMain.cpp ; Check() affiche [OK] / [ECHEC] et compte les echecs.
#include <cstdio>

namespace lwe::test {

inline int g_failures = 0;

inline void Check(bool condition, const char* what) {
    std::printf("[%s] %s\n", condition ? "OK" : "ECHEC", what);
    if (!condition) {
        ++g_failures;
    }
}

inline void Section(const char* name) {
    std::printf("\n== %s ==\n", name);
}

}  // namespace lwe::test
