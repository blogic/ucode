setup common environment:

  $ [ -n "$BUILD_BIN_DIR" ] && export PATH="$BUILD_BIN_DIR:$PATH"
  $ native() {
  >   out=$1; flags=$2; shift 2
  >   "$BUILD_BIN_DIR/ucode" -c,llvm$flags -o "$out.ll" "$@" &&
  >   "$LLVM_LINK" "$out.ll" "$BUILD_BIN_DIR/libucode-native.bc" -o "$out.all.bc" &&
  >   "$OPT" -passes='internalize,default<O1>' \
  >     -internalize-public-api-file="$BUILD_BIN_DIR/libucode-native.api" \
  >     "$out.all.bc" -o "$out.bc" &&
  >   "$LLC" -O1 -relocation-model=pic -filetype=obj "$out.bc" -o "$out.o" &&
  >   "$CC" "$out.o" -lm -ldl -rdynamic -Wl,-rpath,"${RUNPATH-$BUILD_BIN_DIR/native-abi}" -o "$out"
  > }

a native program behaves as the interpreter, also when it fails:

  $ cat > hello.uc <<'EOF'
  > function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
  > print(fib(15), " ", ARGV, " ", defined, "\n");
  > null.x;
  > EOF

  $ native hello ",debug" hello.uc
  $ ./hello --ucode-define defined=42 -- -D x
  Reference error: left-hand side expression is null
  In hello.uc, line 3, byte 6:
  
   `null.x;`
    ~~~~~^~
  
  
  610 [ "-D", "x" ] 42
  [254]

it contains neither the interpreter, the compiler nor the bytecode loader:

  $ nm hello | grep -E ' (uc_vm_execute|uc_vm_execute_chunk|uc_compile_from_source|uc_lexer_init|uc_program_load)$'
  [1]

without the debug flag, a native program carries no debug information and
its stack traces name functions and instruction offsets. The map file
written at build time resolves them later from a log:

  $ native small ",map=hello.map" hello.uc
  $ ./small
  Reference error: left-hand side expression is null
  In function #1 at offset 63 of hello.uc
  
  
  610 [ ] 
  [254]
  $ ./small > small.log 2>&1
  [254]
  $ "$BUILD_BIN_DIR/ucode-trace" hello.map small.log
  Reference error: left-hand side expression is null
  In hello.uc, line 3, byte 6:
  
   `null.x;`
    ~~~~~^~
  
  
  610 [ ] 
  $ printf 'In function foo [C]\n' | "$BUILD_BIN_DIR/ucode-trace" hello.map
  In function foo [C]

the scripts a program loads with literal names are embedded, so that it
runs without its source files:

  $ mkdir -p app/lib app/inc
  $ cat > app/main.uc <<'EOF'
  > let lib = require("lib.counter");
  > print(lib.next(), " ", lib.next(), " ", lib === require("lib.counter"), "\n");
  > include("inc/part.uc", { n: 7 });
  > print(render("inc/page.ut", { items: [1, 2] }), "\n");
  > print(sourcepath(), "\n");
  > EOF
  $ echo 'let n = 0; return { next: () => ++n };' > app/lib/counter.uc
  $ echo 'print("part ", n, "\n"); include("leaf.uc");' > app/inc/part.uc
  $ echo 'print("leaf\n");' > app/inc/leaf.uc
  $ printf '{%% for (let i in items): %%}[{{ i }}]{%% endfor %%}' > app/inc/page.ut

  $ cd app && native ../bundled "" -L "$PWD/*.uc" main.uc && cd ..
  $ mv app app.moved && mkdir app && cd app
  $ ../bundled --ucode-search-path "$PWD/*.uc"
  1 2 true
  part 7
  leaf
  [1][2]
  /.*/app/main.uc (re)
  $ cd .. && rm -r app && mv app.moved app

scripts loaded by computed names are embedded with compile flags, under
the path the program loads them by:

  $ cat > plugins.uc <<'EOF'
  > for (let path in sort(bundle_files()))
  >   if (wildcard(path, "/usr/share/demo/*.uc"))
  >     include(path);
  > let name = "plug" + "in";
  > include(`/usr/share/demo/${name}.uc`);
  > EOF
  $ mkdir demo && echo 'print("plugin\n"); include("helper.uc");' > demo/plugin.uc
  $ echo 'print("helper\n");' > demo/helper.uc

  $ native plugins ",embed=/usr/share/demo=$PWD/demo" plugins.uc
  $ rm -r demo && ./plugins
  helper
  plugin
  helper
  plugin
  helper

a script that fails to compile fails to load as in the interpreter, and
source code compiled at runtime is not supported:

  $ echo 'let = 1;' > broken.uc
  $ echo 'try { include("broken.uc"); } catch (e) { print(e.message, "\n"); }' > load.uc
  $ native load "" load.uc && ./load
  Unable to compile source file '/.*/broken.uc': (re)
  
    | Syntax error: Expecting variable name
    | In line 1, byte 5:
    |
    |  `let = 1;`
    |       ^-- Near here
  
  $ echo 'print(loadstring("return 6 * 7;")(), "\n");' > dynamic.uc
  $ native dynamic "" dynamic.uc && ./dynamic
  Runtime error: Unable to compile source string:
  
    | Source code compilation not supported
  
  In function #1 at offset 19 of dynamic.uc
  
  
  [254]
