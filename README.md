# Arc++ JIT: An implementation of the Arc programming language #

Arc is a dialect of Lisp.

## Build
```
make
```

With [readline](http://cnswww.cns.cwru.edu/php/chet/readline/rltop.html) support,
```
make readline
```

With [MinGW](http://www.mingw.org/),
```
mingw32-make mingw
```

For Visual C++, use .sln file.
For Code::Blocks, use .cbp file.

## Run
```
Usage: arc++ [OPTIONS...] [FILES...]

OPTIONS:
    -h             print this screen.
    -v             print version.
    -e EXPR        evaluate expression.
    -p EXPR        evaluate expression and print result.
    -i             enter interactive REPL after executing.
    --no-jit       disable JIT (use Direct-Threaded Bytecode VM).
    --no-vm        disable VM and JIT (use pure AST interpreter).
    --interp       alias for --no-vm.
```

## Testing
Run the comprehensive test suite across all execution tiers:
```
make test
```
or directly:
```
arc++ tests.arc
```

## Special form
`assign do fn if mac quote`

## Built-in
`* + - / < > apply bound car ccc cdr close coerce cons cos dir dir-exists disp ensure-dir err expt eval file-exists flushout infile int is jit len log macex maptable mod msec mvfile newstring outfile pipe-from quit rand read readline rmfile scar scdr sin sqrt sread stderr stdin stdout string sym system t table tan trunc type write writeb`

## Library
`++ -- <= = >= aand abs accum acons adjoin afn aif alist all alref and andf assoc atend atom avg before best bestn caar cadr carif caris case caselet catch cddr check commonest compare complement compose consif conswhen copy copylist count counts cut dedup def defmemo do1 dotted drain each empty even fill-table find firstn flat for forlen get idfn iflet in insert-sorted insort insortnew intersperse isa isnt iso join keep keys last len< len> let list listtab loop map map1 mappend max med median mem memo memtable merge mergesort min mismatch most multiple n-of nearest no noisy-each nor nthcdr number obj odd on only ontable or orf pair point pop pos positive pr prn pull push pushnew quasiquote rand-choice rand-elt range readfile readfile1 reclist recstring reduce reinsert-sorted rem repeat retrieve rev rfn rotate round roundup rreduce set single some sort split sref sum summing swap tablist testify time time10 tuples trues union uniq unless until vals w/table w/uniq when whenlet while whiler whilet wipe with withs writefile zap`

## Features
* Three-tier execution architecture:
  * Tier 0: Pure tree-walking AST interpreter (`--no-vm` / `--interp`)
  * Tier 1: Direct-threaded Bytecode Virtual Machine with computed gotos (`--no-jit`)
  * Tier 2: Native x86-64 machine code JIT compiler with direct recursive calls (default)
  * Hot-spot compilation (automatic compilation after repeated invocations)
* In-language profiling and benchmarking (`msec`, `time`, `time10`)
* Reference counting garbage collection (shared_ptr)
* First-class continuations (`ccc` / `call/cc`)
* Tail call optimization
* Implicit indexing
* [Syntax sugar](http://arclanguage.github.io/ref/evaluation.html) (`[]`, `~`, `.`, `!`, `:`)

## Benchmark

Recursive Fibonacci benchmark: computing `fib(30)` (2,692,537 function calls).

### Benchmark Code

**Arc++:**
```arc
(def fib (n)
  (if (< n 2)
      n
      (+ (fib (- n 1)) (fib (- n 2)))))

(prn (fib 30))
```

**Python:**
```python
def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

print(fib(30))
```

### Results (`fib(30)`)

| Implementation | Min (ms) | Mean (ms) | Speedup vs Python |
| :--- | :---: | :---: | :---: |
| **Arc++ (JIT, Pure Execution Est.)** | — | **~21.8 ms** | **4.58x** |
| **Arc++ (JIT, Process Total)** | **49.3 ms** | **51.2 ms** | **2.52x** |
| Python 3.14 (Pure Function) | 93.6 ms | 99.8 ms | 1.00x *(baseline)* |
| Python 3.14 (Process Total) | 124.4 ms | 129.0 ms | 1.00x *(baseline)* |
| **Arc++ (Direct-Threaded VM, `--no-jit`)** | **417.8 ms** | **425.1 ms** | **0.30x** *(6.2x faster than AST interpreter)* |
| Arc++ (Legacy AST Interpreter) | 2558.9 ms | 2631.1 ms | 0.05x |

*Environment: Intel Core i5-12400F, Windows 11 x86-64, average of 10 runs.*

## See also
* [Arc Tutorial](http://www.arclanguage.org/tut.txt), [Arc Tutorial (HTML)](https://arclanguage.github.io/tut-stable.html)
* [Arc Documentation](http://arclanguage.github.io/ref/index.html)
* [Try Arc: Arc REPL In Your Web Browser](http://tryarc.org/)

## License ##

   Copyright 2016-2026 Kim, Taegyoon

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

   [http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0)

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
