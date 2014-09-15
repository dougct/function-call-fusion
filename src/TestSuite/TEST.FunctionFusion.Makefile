##===- TEST.FunctionFusion.Makefile-------------------------*- Makefile -*-===##
#
# This recursively traverses the programs, and runs the range-analysis pass
# on each *.linked.rbc bytecode file with -stats set so that it is possible to
# determine which Hello are being analysed in which programs.
# 
# Usage: 
#     make TEST=FunctionFusion summary (short summary)
#     make TEST=FunctionFusion (detailed list with time passes, etc.)
#     make TEST=FunctionFusion report
#     make TEST=FunctionFusion report.html
#
##===----------------------------------------------------------------------===##

CURDIR  := $(shell cd .; pwd)
PROGDIR := $(PROJ_SRC_ROOT)
RELDIR  := $(subst $(PROGDIR),,$(CURDIR))


$(PROGRAMS_TO_TEST:%=test.$(TEST).%): \
test.$(TEST).%: Output/%.$(TEST).report.txt
	@cat $<

$(PROGRAMS_TO_TEST:%=Output/%.$(TEST).report.txt):  \
Output/%.$(TEST).report.txt: Output/%.linked.rbc $(LOPT) \
	$(PROJ_SRC_ROOT)/TEST.FunctionFusion.Makefile 
	$(VERB) $(RM) -f $@
	@echo "---------------------------------------------------------" >> $@
	@echo ">>> ========= '$(RELDIR)/$*' Program" >> $@
	@echo "---------------------------------------------------------" >> $@

	@-$(LOPT) -load /Users/douglas/work/llvm/Debug+Asserts/lib/LLVMFunctionFusion.dylib -func-fusion -debug -stats -time-passes -disable-output $< 2>>$@

summary:
	@$(MAKE) TEST=FunctionFusion

.PHONY: summary
REPORT_DEPENDENCIES := $(LOPT)

