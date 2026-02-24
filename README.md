# 🌉 brise Programming Language (v0.2.0 Alpha)

**brise** is an ultra-lightweight interpreted programming language with a human-readable syntax. The name is inspired by the French word for "breeze": the language is fast, fresh, and free from unnecessary brackets or semicolons.

## 📄 Extension Specification
Official file extension: `.bri`

## ✨ Core Features v0.1
* **Simple Output:** Use `say:` to print text.
* **Variables:** Dynamic value assignment via `set:`.
* **Contextual Injection:** Use variables inside strings using parentheses `(variable)`.
* **Custom Commands:** Create your own functions using the `Command:` keyword.
* **Lists:** Mass data processing with `List:` and the `Say to everyone:` loop.
* **Modularity:** Connect multiple files using `Include:`.

## 🛠 Installation
1. Download `brise_installer.exe` from the **Releases** section.
2. Run the installer. It will automatically add `brise` to your system PATH.
3. Restart your terminal.

## 💻 Code Example
```brise
set:user = "Developer"
say:"Welcome to brise, (user)!"

List: tools (Logic, Speed, Simplicity)
Say to everyone:(say:"Feature: (item)")
```

## 🚀 New in v0.2 (language extensions)

## 📦 Release v0.2.0 Alpha
- Added `calc:` expression evaluation with `+ - * /` and parentheses.
- Extended `if:` with `==`, `!=`, `>`, `<`, `>=`, `<=`.
- `say:` now supports both quoted and raw text modes.
- See `examples/advanced_demo.bri` for a practical script sample.

* **`calc:` arithmetic expressions** with `+ - * /` and parentheses, including numeric variables.
* **Extended `if:` conditions** with `==`, `!=`, `>`, `<`, `>=`, `<=` for numbers and text.
* **Flexible `say:`** now supports either quoted text or raw text after `say:`.

### Example
```brise
set:price = "120"
set:discount = "15"
calc:final = price - discount * 2
if: final > 80 (say:"Final price: (final)")
```

---
# 🌉 brise Language (v0.2.0 Alpha)

**brise** — это сверхлёгкий интерпретируемый язык программирования с человекочитаемым синтаксисом. Название вдохновлено лёгким бризом: язык быстрый, свежий и не перегружен лишними скобками или точками с запятой.

## 📄 Спецификация расширения
Официальное расширение файлов: `.bri`

## ✨ Основные возможности v0.1
* **Простой вывод:** Команда `say:` для печати текста.
* **Переменные:** Динамическая подстановка значений через `set:`.
* **Контекстные вставки:** Использование переменных внутри строк через круглые скобки `(variable)`.
* **Кастомные команды:** Создание собственных функций через ключевое слово `Command:`.
* **Списки:** Массовая обработка данных через `List:` и циклическую команду `Say to everyone:`.
* **Модульность:** Подключение других файлов через `Include:`.

## 🛠 Установка (Классический метод)
На данный момент язык находится в стадии Alpha. 

1. Скачайте `brise_installer.exe` из раздела **Releases**.
2. Запустите установщик. Он автоматически добавит `brise` в системную переменную PATH.
3. Перезагрузите терминал.

## 💻 Примеры кода

### Приветствие и переменные
```brise
set:name = "Разработчик"
say:"Добро пожаловать в brise, (name)!"


## 🌐 Website on GitHub
If the landing page is not visible on GitHub, enable **GitHub Pages** in repository settings and use `index.html` from the root branch.


## 🧱 Interpreter architecture (v0.3.0-dev)
- Lexer-based tokenization (`KEYWORD`, `IDENTIFIER`, `STRING`, `NUMBER`, `OPERATOR`).
- Strong value model via `std::variant<double, std::string, bool>`.
- `Environment` class encapsulates variables, commands, and lists.
- Rich runtime diagnostics with `ErrorContext` (`[Error at line X in file]: ...`).
- Command dispatcher now uses handler registry (no long `if-else` chain) and supports nested actions in `if:(...)`.
- Added `query:` input command (`reply` variable + optional target variable).
- Added text file I/O: `read:` and `write:` commands.
- Added file execution cache: script is tokenized once and reused on next includes/runs in same session.

