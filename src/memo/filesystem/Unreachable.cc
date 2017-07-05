#include <memo/filesystem/Unreachable.hh>

#include <elle/cast.hh>

ELLE_LOG_COMPONENT("memo.filesystem.Unreachable");

namespace memo
{
  namespace filesystem
  {
    /*-------------.
    | Construction |
    `-------------*/

    Unreachable::Unreachable(FileSystem& owner,
                             std::unique_ptr<model::blocks::Block> data,
                             std::shared_ptr<DirectoryData> parent,
                             std::string const& name,
                             Address address,
                             EntryType type)
      : Node(owner, address, parent, name)
      , _data(std::move(data))
      , _type(type)
    {}

    Unreachable::~Unreachable()
    {
      ELLE_DEBUG("%s: destroyed", this);
    }

    /*-----.
    | Path |
    `-----*/

    bool
    Unreachable::allow_cache()
    {
      return false;
    }

    void
    Unreachable::stat(struct stat* st)
    {
      memset(st, 0, sizeof(struct stat));
      st->st_mode = this->_type == EntryType::file ? S_IFREG : S_IFDIR;
    }

    std::string
    Unreachable::getxattr(std::string const& key)
    {
      return umbrella(
        [this, &key] () -> std::string
        {
          if (auto special = xattr_special(key))
          {
            if (*special == "auth")
            {
              return this->perms_to_json(dynamic_cast<ACLBlock&>(*this->_data));
            }
            else if (this->_type == EntryType::directory &&
                     *special == "auth.inherit")
            {
              return "unknown";
            }
          }
          return Node::getxattr(key);
        });
    }

    /*----------.
    | Printable |
    `----------*/

    void
    Unreachable::print(std::ostream& stream) const
    {
      elle::fprintf(stream, "Unreachable(\"%s\")", this->_name);
    }
  }
}
