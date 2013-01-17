/*-------------------------------------------------------------------------*/
/* AMPL/IBM ILOG CP Optimizer driver                         Robert Fourer */
/*                                                                         */
/* Name           : ilogcp.cpp                                             */
/* Title          : AMPL/IBM ILOG CP Optimizer driver                      */
/* By             : Robert Fourer                                          */
/* Date           : October 2000                                           */
/*                                                                         */
/* A driver to link AMPL linear integer programs with ILOG Concert 1.0     */
/* October 2000: Linear/Nonlinear version                                  */
/* June 2012:    Updated to Concert 12.4 (Victor Zverovich)                */
/*-------------------------------------------------------------------------*/
//
// Possible improvements: Some sort of variable preference mechanism.
//
// Reference: "Extending an Algebraic Modeling Language to
// Support Constraint Programming" by Robert Fourer and David M. Gay,
// INFORMS Journal on Computing, Fall 2002, vol. 14, no. 4, 322-344
// (http://joc.journal.informs.org/content/14/4/322).

#include "ilogcp.h"
#include "ilogcp_date.h"

#include <cctype>
#include <cstdlib>

using std::vector;

static char xxxvers[] = "ilogcp_options\0\n"
  "AMPL/IBM ILOG CP Optimizer Driver Version " qYYYYMMDD "\n";

#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif

namespace {

const char *InferenceLevels[] = {
  "default",
  "low",
  "basic",
  "medium",
  "extended",
  0
};

const char *Flags[] = {
  "off",
  "on",
  0
};

const char *SearchTypes[] = {
  "depthfirst",
  "restart",
  "multipoint",
  0
};

const char *Verbosities[] = {
  "quiet",
  "terse",
  "normal",
  "verbose",
  0
};

const char *TimeModes[] = {
  "cputime",
  "elapsedtime",
  0
};

void RequireNonzeroConstRHS(ampl::BinaryExpr e, const std::string &func_name) {
  ampl::NumericConstant num = ampl::Cast<ampl::NumericConstant>(e.rhs());
  if (!num || num.value() != 0) {
    throw ampl::UnsupportedExprError(
        func_name + " with nonzero second parameter");
  }
}
}

namespace ampl {

Optimizer::Optimizer(IloEnv env, const Problem &p) :
  vars_(env, p.num_vars()), cons_(env, p.num_cons()) {}

Optimizer::~Optimizer() {}

double CPLEXOptimizer::GetSolution(Problem &p, fmt::Formatter &format_message,
    vector<double> &primal, vector<double> &dual) const {
  IloNum obj_value = cplex_.getObjValue();
  primal.resize(p.num_vars());
  IloNumVarArray vars = Optimizer::vars();
  for (int j = 0, n = p.num_vars(); j < n; ++j)
    primal[j] = cplex_.getValue(vars[j]);
  if (cplex_.isMIP()) {
    format_message("{0} nodes, ") << cplex_.getNnodes();
  } else {
    dual.resize(p.num_cons());
    IloRangeArray cons = Optimizer::cons();
    for (int i = 0, n = p.num_cons(); i < n; ++i)
      dual[i] = cplex_.getDual(cons[i]);
  }
  format_message("{0} iterations, objective {1}")
    << cplex_.getNiterations() << ObjPrec(obj_value);
  return obj_value;
}

double CPOptimizer::GetSolution(Problem &p, fmt::Formatter &format_message,
    vector<double> &primal, vector<double> &) const {
  format_message("{0} choice points, {1} fails")
      << solver_.getInfo(IloCP::NumberOfChoicePoints)
      << solver_.getInfo(IloCP::NumberOfFails);
  double obj_value = 0;
  if (p.num_objs() > 0) {
    obj_value = solver_.getValue(obj());
    format_message(", objective {0}") << ObjPrec(obj_value);
  }
  primal.resize(p.num_vars());
  IloNumVarArray vars = Optimizer::vars();
  for (int j = 0, n = p.num_vars(); j < n; ++j)
    primal[j] = solver_.getValue(vars[j]);
  return obj_value;
}

IlogCPSolver::IlogCPSolver() :
   Solver<IlogCPSolver>("ilogcp", 0, YYYYMMDD), mod_(env_), gotopttype_(false),
   debug_(false), numberofs_(CreateVar(env_)) {
  options_[DEBUGEXPR] = 0;
  options_[OPTIMIZER] = AUTO;
  options_[TIMING] = 0;
  options_[USENUMBEROF] = 1;

  set_long_name(fmt::Format("ilogcp {0}.{1}.{2}")
      << IloConcertVersion::_ILO_MAJOR_VERSION
      << IloConcertVersion::_ILO_MINOR_VERSION
      << IloConcertVersion::_ILO_TECH_VERSION);
  set_version(fmt::Format("AMPL/IBM ILOG CP Optimizer [{0} {1}.{2}.{3}]")
      << IloConcertVersion::_ILO_NAME << IloConcertVersion::_ILO_MAJOR_VERSION
      << IloConcertVersion::_ILO_MINOR_VERSION
      << IloConcertVersion::_ILO_TECH_VERSION);

  // The following options are not implemented because corresponding
  // constraints are never generated by the driver:
  // - CountInferenceLevel
  // - SequenceInferenceLevel
  // - AllMinDistanceInferenceLevel
  // - ElementInferenceLevel
  // - PrecedenceInferenceLevel
  // - IntervalSequenceInferenceLevel
  // - NoOverlapInferenceLevel
  // - CumulFunctionInferenceLevel
  // - StateFunctionInferenceLevel

  // The options must be in alphabetical order.

  AddStrOption("alldiffinferencelevel",
      "Inference level for 'alldiff' constraints.  Possible values:\n"
      "      0 = default\n"
      "      1 = low\n"
      "      2 = basic\n"
      "      3 = medium\n"
      "      4 = extended\n",
      &IlogCPSolver::SetCPOption,
      CPOptionInfo(IloCP::AllDiffInferenceLevel,
          IloCP::Default, InferenceLevels));

  AddStrOption("branchlimit",
      "Limit on the number of branches made before "
      "terminating a search.  Default = no limit.",
      &IlogCPSolver::SetCPOption, CPOptionInfo(IloCP::BranchLimit));

  AddStrOption("choicepointlimit",
      "Limit on the number of choice points created"
      "before terminating a search.  Default = no limit.",
      &IlogCPSolver::SetCPOption, CPOptionInfo(IloCP::ChoicePointLimit));

  AddStrOption("constraintaggregation",
      "0 or 1 (default 1):  Whether to aggregate basic constraints.",
      &IlogCPSolver::SetCPOption,
      CPOptionInfo(IloCP::ConstraintAggregation, IloCP::Off, Flags));

  AddIntOption("debugexpr",
      "0 or 1 (default 0):  Whether to print debugging "
      "information for expression trees.",
      &IlogCPSolver::SetBoolOption, DEBUGEXPR);

  AddStrOption("defaultinferencelevel",
      "Default inference level for constraints.  Possible values:\n"
      "      1 = low\n"
      "      2 = basic\n"
      "      3 = medium\n"
      "      4 = extended\n",
      &IlogCPSolver::SetCPOption,
      CPOptionInfo(IloCP::DefaultInferenceLevel,
          IloCP::Default, InferenceLevels));

  AddStrOption("distributeinferencelevel",
      "Inference level for 'distribute' constraints.  Possible values:\n"
      "      0 = default\n"
      "      1 = low\n"
      "      2 = basic\n"
      "      3 = medium\n"
      "      4 = extended\n",
      &IlogCPSolver::SetCPOption,
      CPOptionInfo(IloCP::DistributeInferenceLevel,
          IloCP::Default, InferenceLevels));

  AddStrOption("dynamicprobing",
      "Use probing during search.  Possible values:\n"
      "     -1 = auto (default)\n"
      "      0 = off\n"
      "      1 = on\n",
      &IlogCPSolver::SetCPOption,
      CPOptionInfo(IloCP::DynamicProbing, IloCP::Off, Flags, true));

  AddDblOption("dynamicprobingstrength",
      "Effort dedicated to dynamic probing as a factor "
      "of the total search effort.  Default = 0.03.",
      &IlogCPSolver::SetCPDblOption, IloCP::DynamicProbingStrength);

  AddStrOption("faillimit",
      "Limit on the number of failures allowed before terminating a search.  "
      "Default = no limit",
      &IlogCPSolver::SetCPOption, CPOptionInfo(IloCP::FailLimit));

  AddStrOption("logperiod",
      "Specifies how often the information in the search log is displayed.",
      &IlogCPSolver::SetCPOption, CPOptionInfo(IloCP::LogPeriod));

  AddStrOption("logverbosity",
      "Verbosity of the search log.  Possible values:\n"
      "      0 = quiet (default)\n"
      "      1 = terse\n"
      "      2 = normal\n"
      "      3 = verbose\n",
      &IlogCPSolver::SetCPOption,
      CPOptionInfo(IloCP::LogVerbosity, IloCP::Quiet, Verbosities));

  AddIntOption<int>("mipdisplay",
      "Frequency of displaying branch-and-bound information "
      "(for optimizing integer variables):\n"
      "      0 (default) = never\n"
      "      1 = each integer feasible solution\n"
      "      2 = every \"mipinterval\" nodes\n"
      "      3 = every \"mipinterval\" nodes plus\n"
      "          information on LP relaxations\n"
      "          (as controlled by \"display\")\n"
      "      4 = same as 2, plus LP relaxation info.\n"
      "      5 = same as 2, plus LP subproblem info.\n",
      &IlogCPSolver::SetCPLEXIntOption, IloCplex::MIPDisplay);

  AddIntOption<int>("mipinterval",
      "Frequency of node logging for mipdisplay 2 or 3. Default = 1.",
      &IlogCPSolver::SetCPLEXIntOption, IloCplex::MIPInterval);

  AddStrOption("multipointnumberofsearchpoints",
      "Number of solutions for the multi-point search "
      "algorithm.  Default = 30.",
      &IlogCPSolver::SetCPOption,
      CPOptionInfo(IloCP::MultiPointNumberOfSearchPoints));

  AddDblOption("optimalitytolerance",
      "Absolute tolerance on the objective value.  Default = 0.",
      &IlogCPSolver::SetCPDblOption, IloCP::OptimalityTolerance);

  AddStrOption("optimizer",
      "Specifies which optimizer to use.  Possible values:\n"
      "      auto  = CP Optimizer if the problem has\n"
      "              nonlinear objective/constraints\n"
      "              or logical constraints, CPLEX\n"
      "              otherwise (default)\n"
      "      cp    = CP Optimizer\n"
      "      cplex = CPLEX Optimizer\n",
      &IlogCPSolver::SetOptimizer);

  AddStrOption("outlev", "Synonym for \"logverbosity\".",
      &IlogCPSolver::SetCPOption,
      CPOptionInfo(IloCP::LogVerbosity, IloCP::Quiet, Verbosities));

  AddStrOption("propagationlog",
      "Level of propagation trace reporting.  Possible values:\n"
      "      0 = quiet (default)\n"
      "      1 = terse\n"
      "      2 = normal\n"
      "      3 = verbose\n",
      &IlogCPSolver::SetCPOption,
      CPOptionInfo(IloCP::PropagationLog, IloCP::Quiet, Verbosities));

  AddStrOption("randomseed",
      "Seed for the random number generator.  Default = 0.",
      &IlogCPSolver::SetCPOption, CPOptionInfo(IloCP::RandomSeed));

  AddDblOption("relativeoptimalitytolerance",
      "Relative tolerance on the objective value.  Default = 1e-4.",
      &IlogCPSolver::SetCPDblOption, IloCP::RelativeOptimalityTolerance);

  AddStrOption("restartfaillimit",
      "Number of failures allowed before restarting  search.  Default = 100.",
      &IlogCPSolver::SetCPOption, CPOptionInfo(IloCP::RestartFailLimit));

  AddDblOption("restartgrowthfactor",
      "Increase of the number of allowed failures "
      "before restarting search.  Default = 1.05.",
      &IlogCPSolver::SetCPDblOption, IloCP::RestartGrowthFactor);

  AddStrOption("searchtype",
      "Type of search used for solving a problem.  Possible values:\n"
      "      0 = depthfirst\n"
      "      1 = restart (default)\n"
      "      2 = multipoint\n",
      &IlogCPSolver::SetCPOption,
      CPOptionInfo(IloCP::SearchType, IloCP::DepthFirst, SearchTypes, true));

  AddStrOption("solutionlimit",
      "Limit on the number of feasible solutions found "
      "before terminating a search.  Default = no limit.",
      &IlogCPSolver::SetCPOption, CPOptionInfo(IloCP::SolutionLimit));

  AddStrOption("temporalrelaxation",
      "0 or 1 (default 1):  Whether to use temporal relaxation.",
      &IlogCPSolver::SetCPOption,
      CPOptionInfo(IloCP::TemporalRelaxation, IloCP::Off, Flags));

  AddDblOption("timelimit",
      "Limit on the CPU time spent solving before "
      "terminating a search.  Default = no limit.",
      &IlogCPSolver::SetCPDblOption, IloCP::TimeLimit);

  AddStrOption("timemode",
      "Specifies how the time is measured in CP Optimizer.  Possible values:\n"
      "      0 = cputime (default)\n"
      "      1 = elapsedtime\n",
      &IlogCPSolver::SetCPOption,
      CPOptionInfo(IloCP::TimeMode, IloCP::CPUTime, TimeModes));

  AddIntOption("timing",
      "0 or 1 (default 0):  Whether to display timings for the run.\n",
      &IlogCPSolver::SetBoolOption, IlogCPSolver::TIMING);

  AddIntOption("usenumberof",
      "0 or 1 (default 1):  Whether to consolidate 'numberof' expressions "
      "by use of IloDistribute constraints.",
      &IlogCPSolver::SetBoolOption, IlogCPSolver::USENUMBEROF);

  AddStrOption("workers",
      "Number of workers to run in parallel to solve a problem.  "
      "In addition to numeric values this option accepts the value "
      "\"auto\" since CP Optimizer version 12.3.  Default = 1.",
      &IlogCPSolver::SetCPOption, CPOptionInfo(IloCP::Workers, 0, 0, true));
}

IlogCPSolver::~IlogCPSolver() {
  env_.end();
}

void IlogCPSolver::SetOptimizer(const char *name, const char *value) {
  int opt = 0;
  if (strcmp(value, "auto") == 0) {
    opt = AUTO;
  } else if (strcmp(value, "cp") == 0) {
    opt = CP;
  } else if (strcmp(value, "cplex") == 0) {
    opt = CPLEX;
  } else {
    ReportError("Invalid value {0} for option {1}") << value << name;
    return;
  }
  if (!gotopttype_)
    options_[OPTIMIZER] = opt;
}

void IlogCPSolver::SetBoolOption(const char *name, int value, Option opt) {
  if (!gotopttype_)
    return;
  if (value != 0 && value != 1)
    ReportError("Invalid value {0} for option {1}") << value << name;
  else
    options_[opt] = value;
}

void IlogCPSolver::SetCPOption(
    const char *name, const char *value, const CPOptionInfo &info) {
  if (!gotopttype_)
    return;
  CPOptimizer *cp_opt = dynamic_cast<CPOptimizer*>(optimizer_.get());
  if (!cp_opt) {
    ReportError("Invalid option {0} for CPLEX optimizer") << name;
    return;
  }
  try {
    char *end = 0;
    long intval = std::strtol(value, &end, 0);
    if (!*end) {
      if (intval != -1 || !info.accepts_auto)
        intval += info.start;
      cp_opt->solver().setParameter(info.param, intval);
      return;
    }
    if (info.values) {
      // Search for a value in the list of known values.
      // Use linear search since the number of values is small.
      for (int i = 0; info.values[i]; ++i) {
        if (strcmp(value, info.values[i]) == 0) {
          cp_opt->solver().setParameter(info.param, i + info.start);
          return;
        }
      }
    }
    if (info.accepts_auto && strcmp(value, "auto") == 0) {
      cp_opt->solver().setParameter(info.param, IloCP::Auto);
      return;
    }
  } catch (const IloException &) {}
  ReportError("Invalid value {0} for option {1}") << value << name;
}

void IlogCPSolver::SetCPDblOption(
    const char *name, double value, IloCP::NumParam param) {
  if (!gotopttype_)
    return;
  CPOptimizer *cp_opt = dynamic_cast<CPOptimizer*>(optimizer_.get());
  if (!cp_opt) {
    ReportError("Invalid option {0} for CPLEX optimizer") << name;
    return;
  }
  try {
    cp_opt->solver().setParameter(param, value);
  } catch (const IloException &) {
    ReportError("Invalid value {0} for option {1}") << value << name;
  }
}

void IlogCPSolver::SetCPLEXIntOption(const char *name, int value, int param) {
  if (!gotopttype_)
    return;
  CPLEXOptimizer *cplex_opt = dynamic_cast<CPLEXOptimizer*>(optimizer_.get());
  if (!cplex_opt) {
    ReportError("Invalid option {0} for CP optimizer") << name;
    return;
  }
  // Use CPXsetintparam instead of IloCplex::setParam to avoid dealing with
  // two overloads, one for the type int and one for the type long.
  cpxenv *env = cplex_opt->cplex().getImpl()->getCplexEnv();
  if (CPXsetintparam(env, param, value) != 0)
    ReportError("Invalid value {0} for option {1}") << value << name;
}

bool IlogCPSolver::ParseOptions(char **argv) {
  // Get optimizer type.
  gotopttype_ = false;
  if (!Solver<IlogCPSolver>::ParseOptions(
      argv, *this, BasicSolver::NO_OPTION_ECHO)) {
    return false;
  }

  int &opt = options_[OPTIMIZER];
  const Problem &p = problem();
  if (opt == AUTO) {
    opt = CPLEX;
    if (p.num_nonlinear_objs() + p.num_nonlinear_cons() +
        p.num_logical_cons() != 0) {
      opt = CP;
    }
  }
  if (opt == CPLEX)
    optimizer_.reset(new CPLEXOptimizer(env_, p));
  else optimizer_.reset(new CPOptimizer(env_, p));

  // Parse remaining options.
  gotopttype_ = true;
  if (!Solver<IlogCPSolver>::ParseOptions(argv, *this))
    return false;

  debug_ = GetOption(DEBUGEXPR) != 0;
  return true;
}

IloNumExprArray IlogCPSolver::ConvertArgs(VarArgExpr e) {
  IloNumExprArray args(env_);
  for (VarArgExpr::iterator i = e.begin(); *i; ++i)
    args.add(Visit(*i));
  return args;
}

IloExpr IlogCPSolver::VisitIf(IfExpr e) {
  IloConstraint condition(Visit(e.condition()));
  IloNumVar var(env_, -IloInfinity, IloInfinity);
  mod_.add(IloIfThen(env_, condition, var == Visit(e.true_expr())));
  mod_.add(IloIfThen(env_, !condition, var == Visit(e.false_expr())));
  return var;
}

IloExpr IlogCPSolver::VisitAtan2(BinaryExpr e) {
  IloNumExpr y(Visit(e.lhs())), x(Visit(e.rhs()));
  IloNumExpr atan(IloArcTan(y / x));
  IloNumVar result(env_, -IloInfinity, IloInfinity);
  mod_.add(IloIfThen(env_, x >= 0, result == atan));
  mod_.add(IloIfThen(env_, x <= 0 && y >= 0, result == atan + M_PI));
  mod_.add(IloIfThen(env_, x <= 0 && y <= 0, result == atan - M_PI));
  return result;
}

IloExpr IlogCPSolver::VisitSum(SumExpr e) {
  IloExpr sum(env_);
  for (SumExpr::iterator i = e.begin(), end = e.end(); i != end; ++i)
    sum += Visit(*i);
  return sum;
}

IloExpr IlogCPSolver::VisitRound(BinaryExpr e) {
  RequireNonzeroConstRHS(e, "round");
  // Note that IloOplRound rounds half up.
  return IloOplRound(Visit(e.lhs()));
}

IloExpr IlogCPSolver::VisitTrunc(BinaryExpr e) {
  RequireNonzeroConstRHS(e, "trunc");
  return IloTrunc(Visit(e.lhs()));
}

IloExpr IlogCPSolver::VisitCount(CountExpr e) {
  IloExpr sum(env_);
  for (CountExpr::iterator i = e.begin(), end = e.end(); i != end; ++i)
    sum += Visit(*i);
  return sum;
}

IloExpr IlogCPSolver::VisitNumberOf(NumberOfExpr e) {
  NumericExpr value = e.value();
  NumericConstant num = Cast<NumericConstant>(value);
  if (num && GetOption(USENUMBEROF))
    return numberofs_.Add(num.value(), e);
  IloExpr sum(env_);
  IloExpr concert_value(Visit(value));
  for (NumberOfExpr::iterator i = e.begin(), end = e.end(); i != end; ++i)
    sum += (Visit(*i) == concert_value);
  return sum;
}

IloExpr IlogCPSolver::VisitPLTerm(PiecewiseLinearTerm t) {
  IloNumArray slopes(env_), breakpoints(env_);
  int num_breakpoints = t.num_breakpoints();
  for (int i = 0; i < num_breakpoints; ++i) {
    slopes.add(t.GetSlope(i));
    breakpoints.add(t.GetBreakpoint(i));
  }
  slopes.add(t.GetSlope(num_breakpoints));
  return IloPiecewiseLinear(vars_[t.var_index()], breakpoints, slopes, 0, 0);
}

IloConstraint IlogCPSolver::VisitExists(IteratedLogicalExpr e) {
  IloOr disjunction(env_);
  for (IteratedLogicalExpr::iterator
      i = e.begin(), end = e.end(); i != end; ++i) {
    disjunction.add(Visit(*i));
  }
  return disjunction;
}

IloConstraint IlogCPSolver::VisitForAll(IteratedLogicalExpr e) {
  IloAnd conjunction(env_);
  for (IteratedLogicalExpr::iterator
      i = e.begin(), end = e.end(); i != end; ++i) {
    conjunction.add(Visit(*i));
  }
  return conjunction;
}

IloConstraint IlogCPSolver::VisitImplication(ImplicationExpr e) {
  IloConstraint condition(Visit(e.condition()));
  return IloIfThen(env_,  condition, Visit(e.true_expr())) &&
      IloIfThen(env_, !condition, Visit(e.false_expr()));
}

IloConstraint IlogCPSolver::VisitAllDiff(AllDiffExpr e) {
  IloIntVarArray vars(env_);
  for (AllDiffExpr::iterator i = e.begin(), end = e.end(); i != end; ++i) {
    if (Variable var = Cast<Variable>(*i)) {
      vars.add(vars_[var.index()]);
      continue;
    }
    IloIntVar var(env_, IloIntMin, IloIntMax);
    mod_.add(var == Visit(*i));
    vars.add(var);
  }
  return IloAllDiff(env_, vars);
}

void IlogCPSolver::FinishBuildingNumberOf() {
  for (IlogNumberOfMap::iterator
      i = numberofs_.begin(), end = numberofs_.end(); i != end; ++i) {
    int index = 0;
    const IlogNumberOfMap::ValueMap &val_map = i->values;
    IloIntVarArray cards(env_, val_map.size());
    IloIntArray values(env_, val_map.size());
    for (IlogNumberOfMap::ValueMap::const_iterator j = val_map.begin(),
        val_end = val_map.end(); j != val_end; ++j, ++index) {
      values[index] = static_cast<IloInt>(j->first);
      cards[index] = j->second;
    }

    index = 0;
    NumberOfExpr expr = i->expr;
    IloIntVarArray vars(env_, expr.num_args());
    for (NumberOfExpr::iterator
        j = expr.begin(), expr_end = expr.end(); j != expr_end; ++j, ++index) {
      IloIntVar var(env_, IloIntMin, IloIntMax);
      vars[index] = var;
      mod_.add(var == Visit(*j));
    }

    mod_.add(IloDistribute(env_, cards, values, vars));
  }
}

int IlogCPSolver::Run(char **argv) {
  // Initialize timers.
  double Times[5];
  Times[0] = xectim_();

  Problem &problem = Solver<IlogCPSolver>::problem();
  if (!ReadProblem(argv) || !ParseOptions(argv))
    return 1;

  // Set up optimization problem in ILOG Concert.

  vars_ = optimizer_->vars();

  int n_var_cont = problem.num_continuous_vars();
  if (n_var_cont != 0 && GetOption(OPTIMIZER) == CP) {
    ReportError("CP Optimizer doesn't support continuous variables");
    return 1;
  }
  for (int j = 0; j < n_var_cont; j++) {
    vars_[j] = IloNumVar(env_,
        problem.GetVarLB(j), problem.GetVarUB(j), ILOFLOAT);
  }
  for (int j = n_var_cont; j < problem.num_vars(); j++) {
    vars_[j] = IloNumVar(env_,
        problem.GetVarLB(j), problem.GetVarUB(j), ILOINT);
  }

  if (problem.num_objs() > 0) {
    NumericExpr expr(problem.GetNonlinearObjExpr(0));
    NumericConstant constant(Cast<NumericConstant>(expr));
    IloExpr ilo_expr(env_, constant ? constant.value() : 0);
    if (problem.num_nonlinear_objs() > 0)
      ilo_expr += Visit(expr);
    LinearObjExpr linear = problem.GetLinearObjExpr(0);
    for (LinearObjExpr::iterator
        i = linear.begin(), end = linear.end(); i != end; ++i) {
      ilo_expr += i->coef() * vars_[i->var_index()];
    }
    IloObjective MinOrMax(env_, ilo_expr,
        problem.GetObjType(0) == Problem::MIN ?
        IloObjective::Minimize : IloObjective::Maximize);
    optimizer_->set_obj(MinOrMax);
    IloAdd(mod_, MinOrMax);
  }

  if (int n_cons = problem.num_cons()) {
    IloRangeArray cons(optimizer_->cons());
    for (int i = 0; i < n_cons; ++i) {
      IloExpr conExpr(env_);
      LinearConExpr linear = problem.GetLinearConExpr(i);
      for (LinearConExpr::iterator
          j = linear.begin(), end = linear.end(); j != end; ++j) {
        conExpr += j->coef() * vars_[j->var_index()];
      }
      if (i < problem.num_nonlinear_cons())
        conExpr += Visit(problem.GetNonlinearConExpr(i));
      cons[i] = (problem.GetConLB(i) <= conExpr <= problem.GetConUB(i));
    }
    mod_.add(cons);
  }

  if (int n_lcons = problem.num_logical_cons()) {
    IloConstraintArray lcons(env_, n_lcons);
    for (int i = 0; i < n_lcons; ++i)
      lcons[i] = Visit(problem.GetLogicalConExpr(i));
    mod_.add(lcons);
  }

  FinishBuildingNumberOf();

  int timing = GetOption(TIMING);
  Times[1] = timing ? xectim_() : 0;

  // Solve the problem.
  IloAlgorithm alg(optimizer_->algorithm());
  alg.extract (mod_);
  SignalHandler sh(*this, optimizer_.get());
  Times[2] = timing ? xectim_() : 0;
  IloBool successful = alg.solve();
  Times[3] = timing ? xectim_() : 0;

  // Convert solution status.
  int solve_code = 0;
  const char *status;
  switch (alg.getStatus()) {
  default:
    // Fall through.
  case IloAlgorithm::Unknown:
    if (optimizer_->interrupted()) {
      solve_code = 600;
      status = "interrupted";
    } else {
      solve_code = 501;
      status = "unknown solution status";
    }
    break;
  case IloAlgorithm::Feasible:
    if (optimizer_->interrupted()) {
      solve_code = 600;
      status = "interrupted";
    } else {
      solve_code = 100;
      status = "feasible solution";
    }
    break;
  case IloAlgorithm::Optimal:
    solve_code = 0;
    status = "optimal solution";
    break;
  case IloAlgorithm::Infeasible:
    solve_code = 200;
    status = "infeasible problem";
    break;
  case IloAlgorithm::Unbounded:
    solve_code = 300;
    status = "unbounded problem";
    break;
  case IloAlgorithm::InfeasibleOrUnbounded:
    solve_code = 201;
    status = "infeasible or unbounded problem";
    break;
  case IloAlgorithm::Error:
    solve_code = 500;
    status = "error";
    break;
  }
  problem.set_solve_code(solve_code);

  fmt::Formatter format_message;
  format_message("{0}: {1}\n") << long_name() << status;
  vector<real> primal, dual;
  double obj_value = std::numeric_limits<double>::quiet_NaN();
  if (successful)
    obj_value = optimizer_->GetSolution(problem, format_message, primal, dual);
  HandleSolution(format_message.c_str(), primal.empty() ? 0 : &primal[0],
      dual.empty() ? 0 : &dual[0], obj_value);

  if (timing) {
    Times[4] = xectim_();
    std::cerr << "\n"
        << "Define = " << Times[1] - Times[0] << "\n"
        << "Setup =  " << Times[2] - Times[1] << "\n"
        << "Solve =  " << Times[3] - Times[2] << "\n"
        << "Output = " << Times[4] - Times[3] << "\n";
  }
  return 0;
}
}
