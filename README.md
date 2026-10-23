# Auto Decor (Geode mod for Geometry Dash)

## Сборка
1. Установите Geode CLI и SDK (https://docs.geode-sdk.org/getting-started/), задайте `GEODE_SDK`.
2. Рекомендуется: `geode new` -> замените `src/`, `mod.json`, `about.md` файлами из этого архива
   (или соберите прямо из этой папки: `geode build`).
3. В `mod.json` поменяйте `id` и `developer` на свои.
4. Готовый `*.geode` лежит в `build/`, положите его в папку `geode/mods`.

## Как пользоваться
Выделите блоки конструкции в редакторе -> кнопки слева: **Decor / Reroll / Undo / Pal**.
Перед первым запуском выделите ОДИН НЕ-solid декор-объект (квадрат из вкладки Outline) и нажмите **Pal**.

## Проверка генератора без игры
`src/Decorator.*` не зависит от Geode:

    g++ -std=c++17 -O1 -o preview tools/preview.cpp src/Decorator.cpp
    ./preview gate gate 7 6 | python3 tools/render.py out.png     # форма, стиль, seed, detail
    g++ -std=c++17 -O2 -fsanitize=address,undefined -o stress tools/stress.cpp src/Decorator.cpp && ./stress
