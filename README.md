# FREQUENCY — VST3 radio tuner (work in progress)

Статус: аудиодвижок и тесты проверены; JUCE/VST3-часть ещё НЕ собиралась. UI временный.

## Быстрый тест
1. Залейте проект в новый GitHub-репозиторий (ветка main).
2. Вкладка **Actions** -> workflow `build`:
   - `core-tests-linux` — тесты DSP и движка (ASan+UBSan).
   - `windows-vst3` — сборка Windows x64 VST3 (JUCE 8.0.4 через FetchContent). Артефакт: `FREQUENCY-vst3-win-x64`.
3. При ошибке сборки пришлите лог упавшего шага.

## Локальная сборка (Windows, Visual Studio 2022 + CMake)
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release
    ctest --test-dir build -C Release --output-on-failure

Результат: `build/Frequency_artefacts/Release/VST3/FREQUENCY.vst3`
(копируйте вручную в `%LOCALAPPDATA%\Programs\Common\VST3`; сборка ничего не устанавливает).

## Только тесты ядра (любой ОС с g++/clang++)
    g++ -std=c++17 -O1 -g -fsanitize=address,undefined -ISource tests/test_dsp.cpp -o dsp_tests && ./dsp_tests
    g++ -std=c++17 -O1 -g -fsanitize=address,undefined -ISource tests/test_engine.cpp Source/Engine/RadioEngine.cpp Source/Stations/StationPrep.cpp -o engine_tests && ./engine_tests

Архитектура и ограничения: `docs/ARCHITECTURE.md`.
