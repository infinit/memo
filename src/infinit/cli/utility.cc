#include <infinit/cli/utility.hh>

#include <boost/regex.hpp>

namespace infinit
{
  namespace cli
  {
    /// Perform metavariable substitution.
    std::string
    VarMap::expand(std::string const& s) const
    {
      static const auto re = boost::regex("\\{\\w+\\}");
      // Not available in std::.
      return boost::regex_replace(s,
                                  re,
                                  [this] (boost::smatch in)
                                  {
                                    auto k = in.str();
                                    return this->vars.at(k.substr(1, k.size() - 2));
                                  });
    }
  }
}
