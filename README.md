# luau-minify

experimental C++ reimplementation of [wyatt](https://github.com/httpget)'s luau minifier, licensed freely under the Apache 2.0 license

## TODO

- ensure binary uses tailcalls
- switch from std::unordered_map to a performant replacement
- use 2 passes to allow for global and local analysis for maximum character saving
- make strings more efficent

### Building

Regular linux distros:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Minifier.CLI --config Release
```

Nix:

```bash
nix run ".?submodules=1#"
```
