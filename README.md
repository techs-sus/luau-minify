# luau-minify

experimental C++ reimplementation of [wyatt](https://github.com/httpget)'s luau minifier, licensed freely under the Apache 2.0 license

## TODO

- ensure binary uses tailcalls
- our main focus is safety, stability, and quality. performance is probably not good.

### Building

Regular linux distros:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Minifier.CLI --config Release
```

Nix:

```bash
nix run ".?submodules=1#"
```
