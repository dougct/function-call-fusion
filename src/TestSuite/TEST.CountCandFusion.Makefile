##===- TEST.CountCandFusion.Makefile -----------------------*- Makefile -*-===##
#
# This recursively traverses the programs, and runs the range-analysis pass
# on each *.linked.rbc bytecode file with -stats set so that it is possible to
# determine which CountPredCand are being analysed in which programs.
#
# Usage:
#     make TEST=CountCandFusion summary (short summary)
#     make TEST=CountCandFusion (detailed list with time passes, etc.)
#     make TEST=CountCandFusion report
#     make TEST=CountCandFusion report.html
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
	$(PROJ_SRC_ROOT)/TEST.CountCandFusion.Makefile
	$(VERB) $(RM) -f $@
	@echo "---------------------------------------------------------" >> $@
	@echo ">>> ========= '$(RELDIR)/$*' Program" >> $@
	@echo "---------------------------------------------------------" >> $@

	@-$(LOPT) -mem2reg -load \
	/Users/douglas/work/llvm/Debug+Asserts/lib/LLVMCountCandFusion.dylib \
	-cnt-ff -stats -time-passes -debug -disable-output $< 2>>$@

summary:
	@$(MAKE) TEST=CountCandFusion

.PHONY: summary
REPORT_DEPENDENCIES := $(LOPT)

