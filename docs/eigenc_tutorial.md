# Eigen-C (ec) Quick Start Guide

Eigen-C is a compact scripting language for Eigen OS. It supports variables, math, strings, input/output, conditionals, loops, and boolean logic.

## 1. Program Structure

Eigen C programs can use a `void main() { ... }` function or just a sequence of top-level statements. Semicolons are optional.

**Script style:**
```c
print("Hello Eigen!", endl)
exit(0)
```

**Function style:**
```c
void main() {
    print("Hello", endl)
    exit(0)
}
```

## 2. Variables & Data Types

Eigen C supports five types:

| Type | Description |
| :--- | :--- |
| `int` | 32-bit signed integer |
| `double` | Fixed-point decimal (6 decimal places internally) |
| `float` | Alias of `double` |
| `string` | Text string |
| `bool` | Boolean (`true` / `false`) |

### Integers & Math
```c
int a = 10
int b = -5
int sum = a + b
a++        // Increment
b--        // Decrement
print("Sum: ", sum, endl)
```

### Decimals (`double`)
Eigen C supports `double` using fixed-point arithmetic. Decimal literals are supported.
```c
double side_a = 8.0
double side_b = 5.0
double ratio = side_a / side_b
print("Ratio: ", ratio, endl) // Prints: 1.600000
```

### Strings
```c
string greeting = "Hello, Sidney!"
print(greeting, endl)

string name = "Eigen-C"
greeting = "Welcome to "
print(greeting, name, "!", endl)
```

### Booleans
```c
bool active = true
bool done = false
print("Active: ", active, endl)
```

### Integer vs. Floating-Point Math
`int` values are stored as plain integers; `double`/`float` values are
stored as fixed-point numbers (value × 1,000,000). The compiler picks the
correct arithmetic automatically:

```c
int a = 7
print(a * a, endl)       // 49  (integer multiply)
print(7 * 7, endl)       // 49
print(a / 3, endl)       // 2   (integer division truncates)

double x = 2.5
print(x * 2, endl)       // 5.000000  (double multiply)
print(10.5 / 3, endl)    // 3.500000
print(x + x, endl)       // 5.000000
```

An `int` literal passed to a `double` parameter (or used in a `double`
expression) is automatically converted to fixed-point.

### Fixed-Size Arrays
Declare an array with a size in brackets: `type name[size]`. Indices start at `0`.
Array elements can be read, assigned, used in arithmetic, printed, and passed
to functions.

```c
int scores[3]
scores[0] = 85
scores[1] = 92
scores[2] = 78
int sum = scores[0] + scores[1] + scores[2]
print("Total Score: ", sum, endl)   // Total Score: 255
```

`double` arrays work the same way, and string arrays hold string values:

```c
double prices[2]
prices[0] = 2.5
prices[1] = 5.0
print(prices[0] * prices[1], endl)   // 12.500000

string names[3]
names[0] = "alice"
names[2] = "carol"
print(names[0], " and ", names[2], endl)  // alice and carol
```

The index can be any integer expression (including another variable). Out-of-range
indexes are caught at runtime:

```c
int a[2]
a[5] = 1   // runtime error: Array index out of bounds
```

## 3. Functions

Eigen C supports user-defined functions. Every return type works: `int`,
`double`, `bool`, `string`, and `float` (as well as `void` functions that
return nothing).

```c
int add_num(int a, int b) {
    return a + b
}

double half(double x) {
    return x / 2
}

bool is_big(int x) {
    return x > 10
}

string greet(string name) {
    return name
}

void main() {
    print(add_num(3, 4), endl)     // 7
    print(half(5), endl)           // 2.500000  (int literal auto-scaled)
    print(is_big(20), endl)        // 1
    print(greet("world"), endl)    // world
}
```

Notes:
- Function parameters are typed and their count is checked at compile time.
- `float` parameters/returns behave identically to `double`.
- Passing an integer literal where a `double` is expected auto-converts it.

## 4. Flow Control & Input

### If-Else
```c
if (a > b) {
    print("A is greater", endl)
} else {
    print("B is greater or equal", endl)
}
```

### While Loops
```c
int i = 0
while (i < 3) {
    print("i: ", i, endl)
    i++
}
```

### For Loops
```c
for (int i = 0; i < 5; i++) {
    print("i: ", i, endl)
}
```

### User Input (`input`)
The `input()` function reads from the terminal. It automatically parses based on the variable's type.
```c
print("Enter a whole number: ")
int code
input(code)
print("You entered: ", code, endl)

print("Enter a decimal: ")
double measurement
input(measurement)
print("You entered: ", measurement, endl)

print("Enter text: ")
string name
input(name)
print("Hello, ", name, endl)
```

## 5. Operators

| Category | Operators |
| :--- | :--- |
| **Arithmetic** | `+`, `-`, `*`, `/`, `++`, `--` |
| **Comparison** | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| **Logical** | `&&`, `||` |
| **Assignment** | `=` |

## 6. Built-in Functions

| Function | Description |
| :--- | :--- |
| `print(...)` | Print values to terminal. Accepts strings, variables, and `endl`. |
| `endl` | Print a newline. Use inside `print()`. |
| `input(x)` | Read user input into variable `x`. Type is inferred from variable. |
| `exit(n)` | Exit the program with status code `n`. |

## 7. Comments

Single-line comments are supported:
```c
// This is a comment
int x = 5  // Inline comment works too
```

## 8. String Escapes

Use `\n` for newlines inside string literals:
```c
print("Line 1\nLine 2", endl)
```

## 9. Workflow

1. **Write:** Use `edim` editor or write a `.ec` file
2. **Compile:** `ecc <file.ec>` (outputs `<file>.bin`)
3. **Run:** `erun <file.bin>`

## 10. Example Programs

### Hello World
```c
print("Hello Eigen!", endl)
```

### Functions with Return Values
```c
double convert_cm_to_in(double cm) {
    return cm / 2.54
}

int triple(int n) {
    return n * 3
}

string shout(string word) {
    return word
}

void main() {
    print(convert_cm_to_in(10), endl)   // 3.937007
    print(triple(4), endl)              // 12
    print(shout("hi!"), endl)           // hi!
}
```

### Calculator
```c
void main() {
    print("Enter first number: ")
    int a
    input(a)
    
    print("Enter second number: ")
    int b
    input(b)
    
    print("Sum: ", a + b, endl)
    print("Product: ", a * b, endl)
    
    exit(0)
}
```

### Temperature Converter
```c
void main() {
    print("Enter Celsius: ")
    double c
    input(c)
    
    double f = c * 9 / 5 + 32
    print("Fahrenheit: ", f, endl)
    
    exit(0)
}
```

### Guessing Game
```c
void main() {
    int secret = 42
    int guess = 0
    int tries = 0
    
    while (guess != secret) {
        print("Guess: ")
        input(guess)
        tries++
        
        if (guess == secret) {
            print("Correct! Tries: ", tries, endl)
        } else if (guess < secret) {
            print("Higher!", endl)
        } else {
            print("Lower!", endl)
        }
    }
    
    exit(0)
}
```

## 11. Language Reference

| Feature | Syntax |
| :--- | :--- |
| **Types** | `int`, `double`, `float`, `string`, `bool` |
| **Arrays** | `type name[size]` — zero-based, `arr[i]` read/write, bounds-checked at runtime |
| **Functions** | `type name(params) { ... return ... }` — all return types supported |
| **Arithmetic** | `+`, `-`, `*`, `/`, `++`, `--` |
| **Comparison** | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| **Logical** | `&&`, `||` |
| **Control flow** | `if-else`, `while`, `for` |
| **Input/Output** | `print(...)`, `input(x)`, `exit(n)`, `endl` |
| **Comments** | `//` |
