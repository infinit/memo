#include <elle/finally.hh>
#include <elle/log.hh>
#include <elle/memory.hh>

#include <reactor/fsm/CatchTransition.hh>
#include <reactor/fsm/EndTransition.hh>
#include <reactor/fsm/Machine.hh>
#include <reactor/fsm/WaitableTransition.hh>
#include <reactor/scheduler.hh>
#include <reactor/signal.hh>

ELLE_LOG_COMPONENT("reactor.fsm.Machine")

namespace reactor
{
  namespace fsm
  {
    /*-------------.
    | Construction |
    `-------------*/

    Machine::Machine():
      _states(),
      _start(nullptr)
    {}

    /*-------.
    | States |
    `-------*/

    State&
    Machine::state_add(std::unique_ptr<State> state)
    {
      bool first = this->_states.empty();
      State* naked = state.get();
      this->_states.insert(std::move(state));
      if (first)
        this->_start = naked;
      return *naked;
    }

    /*------------.
    | Transitions |
    `------------*/

    Transition&
    Machine::transition_add(State& start,
                            State& end,
                            Waitables const& trigger,
                            bool preemptive)
    {
      std::unique_ptr<Transition> transition(
        new WaitableTransition(start, end, trigger, preemptive));
      Transition& res = *transition.get();
      this->_transitions.insert(std::move(transition));
      return res;
    }

    Transition&
    Machine::transition_add(State& start,
                            State& end)
    {
      std::unique_ptr<Transition> transition(
        new EndTransition(start, end));
      Transition& res = *transition.get();
      this->_transitions.insert(std::move(transition));
      return res;
    }

    Transition&
    Machine::transition_add_catch(State& start,
                                  State& end)
    {
      std::unique_ptr<Transition> transition(
        new CatchTransition(start, end));
      Transition& res = *transition.get();
      this->_transitions.insert(std::move(transition));
      return res;
    }

    /*----------.
    | Execution |
    `----------*/

    void
    Machine::run()
    {
      ELLE_TRACE_SCOPE("%s: run", *this);
      this->_running = true;
      State* current = this->_start;
      while (current = this->_run_state(current))
        /* nothing */;
      this->_running = false;
    }

    State*
    Machine::_run_state(State* state)
    {
      ELLE_TRACE_SCOPE("%s: run %s", *this, *state);
      ELLE_ASSERT(Scheduler::scheduler());
      auto& sched = *Scheduler::scheduler();
      auto action = [&] () {
        try
        {
          state->_action();
        }
        catch (elle::Exception&)
        {
          ELLE_DEBUG("%s: forward exception: %s",
                     *this, elle::exception_string());
          this->_exception = std::current_exception();
          return;
        };
      };
      Thread action_thread(sched, elle::sprintf("%s action", *state), action);
      reactor::Signal triggered;
      Transition* trigger(nullptr);
      std::vector<std::unique_ptr<Thread>> transitions;
      elle::Finally terminate_transitions([&] () {
          for (auto& thread: transitions)
            thread->terminate_now();
        });
      for (auto transition: state->transitions_out())
      {
        std::unique_ptr<Thread> thread(
          transition->run(triggered, trigger, action_thread).release());
        if (thread)
          transitions.emplace_back(thread.release());
      }
      state->_entered.signal();
      elle::Finally exited([&]() {state->_exited.signal(); });
      sched.current()->wait(action_thread);
      state->_done.signal();
      ELLE_DEBUG("%s: state action finished", *this);
      for (auto transition: state->transitions_out())
        transition->done(trigger, this->_exception);
      if (this->_exception)
      {
        ELLE_WARN("%s: state action threw: %s",
                  *this, elle::exception_string(this->_exception));
        std::rethrow_exception(this->_exception);
      }
      if (state->transitions_out().empty())
      {
        ELLE_DEBUG("%s: end state, leaving", *this);
        return nullptr;
      }
      while (!trigger)
      {
        ELLE_DEBUG("%s: waiting for transition", *this);
        sched.current()->wait(triggered);
      }
      if (trigger->action())
        trigger->action()();
      return &trigger->end();
    }

    /*----------.
    | Printable |
    `----------*/

    void
    Machine::print(std::ostream& stream) const
    {
      stream << "FSM " << this;
    }
  }
}
