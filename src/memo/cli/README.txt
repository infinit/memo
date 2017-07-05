* To add a new object Thing, one needs to change several things:

- create src/cli/infinit/Thing.{hh,cc}.
- fwddecl in src/cli/infinit/fwd.hh.
- add the compilation in drakefile, `infinit_sources`.
- instantiate `Thing thing = *this;` in `src/cli/infinit/Infinit.hh`.
- add instantiation in drakefile, `src/infinit/cli/Infinit-instantiate-%s.o`.
- instantiate at the bottom of `src/infinit/cli/Object.cc`.
