#include "timing_reports.h"

#include "vtr_log.h"

#include "tatum/TimingReporter.hpp"

#include "vpr_types.h"
#include "globals.h"

#include "timing_info.h"
#include "timing_util.h"

#include "VprTimingGraphResolver.h"

void generate_setup_timing_stats(const SetupTimingInfo& timing_info, const AnalysisDelayCalculator& delay_calc, bool detailed_reports) {
#ifdef ENABLE_CLASSIC_VPR_STA
    vtr::printf("\n");
    vtr::printf("New Timing Stats\n");
    vtr::printf("================\n");
#endif

    auto& timing_ctx = g_vpr_ctx.timing();
    auto& atom_ctx = g_vpr_ctx.atom();

    print_setup_timing_summary(*timing_ctx.constraints, *timing_info.setup_analyzer());

    VprTimingGraphResolver resolver(atom_ctx.nlist, atom_ctx.lookup, *timing_ctx.graph, delay_calc);
    resolver.set_detailed(detailed_reports);

    tatum::TimingReporter timing_reporter(resolver, *timing_ctx.graph, *timing_ctx.constraints);

    timing_reporter.report_timing_setup("report_timing.setup.rpt", *timing_info.setup_analyzer());
    timing_reporter.report_skew_setup("report_skew.setup.rpt", *timing_info.setup_analyzer());
    timing_reporter.report_unconstrained_setup("report_unconstrained_timing.setup.rpt", *timing_info.setup_analyzer());
}

void generate_hold_timing_stats(const HoldTimingInfo& timing_info, const AnalysisDelayCalculator& delay_calc, bool detailed_reports) {
    auto& timing_ctx = g_vpr_ctx.timing();
    auto& atom_ctx = g_vpr_ctx.atom();

    print_hold_timing_summary(*timing_ctx.constraints, *timing_info.hold_analyzer());

    VprTimingGraphResolver resolver(atom_ctx.nlist, atom_ctx.lookup, *timing_ctx.graph, delay_calc);
    resolver.set_detailed(detailed_reports);

    tatum::TimingReporter timing_reporter(resolver, *timing_ctx.graph, *timing_ctx.constraints);

    timing_reporter.report_timing_hold("report_timing.hold.rpt", *timing_info.hold_analyzer());
    timing_reporter.report_skew_hold("report_skew.hold.rpt", *timing_info.hold_analyzer());
    timing_reporter.report_unconstrained_hold("report_unconstrained_timing.hold.rpt", *timing_info.hold_analyzer());
}
