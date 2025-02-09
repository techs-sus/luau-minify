# luau-minify

experimental C++ reimplementation of [wyatt](https://github.com/httpget)'s luau minifier, licensed freely under the Apache 2.0 license

## Building

Regular linux distrobutions:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Minifier.CLI --config Release
```

Nix:

```bash
nix run ".?submodules=1#"
```
