Function Call Fusion in LLVM
============================

An implementation of function call fusion in LLVM. For more details, check [here](http://homepages.dcc.ufmg.br/~douglas/research/comp-mmt-archs.html).

This repository contains an implementation of function call fusion in LLVM, and it's organized as follows:

  - The src directory contains the source files of our implementation, and is organized like this:
    + CountPredCand: implementation of a pass that counts opportunities for function call fusion;
    + FunctionFusion: implementation of function call fusion in LLVM;
    + TestSuite: test suite makefiles and reports used to gather stats for both implementations described above.




