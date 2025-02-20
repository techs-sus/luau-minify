# luau-minify

experimental C++ reimplementation of [wyatt](https://github.com/httpget)'s luau
minifier, licensed freely under the Apache 2.0 license

## TODO

- ensure binary uses tailcalls
- our main focus is safety, stability, and quality. performance is probably not
  good
- make AstTracking track all uses of locals, make it produce the BlockInfo
  hierachy, and save characters when possible. (example: if fn does not use
  locals a, b, c, d, ..., it can shadow them in order to save characters)
- use DAG (Directed Acrylic Graph) to optimize out locals and shadow if
  profitable

### Building

Regular linux distros:

```bash
# assuming the current working directory is the repository, with all submodules cloned
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Minifier.CLI --config Release
```

Nix:

```bash
nix run ".?submodules=1#"
```
