#include <infinit/grpc/serializer.hh>

#include <elle/serialization/json/Error.hh> // serialization::MissingKey.
#include <grpc++/grpc++.h>
#include <google/protobuf/message.h>

ELLE_LOG_COMPONENT("infinit.grpc.serialization");

namespace
{
  std::string
  uppercase_to_underscore(std::string const& src)
  {
    std::string res;
    for (unsigned int i=0; i<src.size(); ++i)
    {
      auto c = src[i];
      if (c == '.')
        continue;
      if (std::tolower(c) != c && i != 0)
        res += "_";
      res += std::tolower(c);
    }
    return res;
  }

  std::string
  filter_field_name(std::string const& name)
  {
    std::string res;
    for (auto c: name)
    {
      if (c == '.')
        continue;
      res += c;
    }
    return res;
  }
}

namespace infinit
{
  namespace grpc
  {
    std::string
    cxx_to_message_name(std::string name)
    {
      auto p = name.find_first_of('<');
      if (p != name.npos)
      {
        if (name.find("unique_ptr") != name.npos
          || name.find("shared_ptr") != name.npos)
        name = name.substr(p+1);
        else
          name = name.substr(0, p);
      }
      p = name.find_last_of(':');
      if (p != name.npos)
        name = name.substr(p+1);
      p = name.find_first_of('>');
      name = name.substr(0, p);
      if (name == "basic_string")
        name = "string";
      name = name.substr(0, name.find_first_of(" "));
      return uppercase_to_underscore(name);
    }

    SerializerIn::SerializerIn(google::protobuf::Message const* msg)
    : elle::serialization::SerializerIn(false)
    {
      _field = nullptr;
      _message_stack.push_back(msg);
    }

    bool
    SerializerIn::_enter(std::string const& _name)
    {
      ELLE_DUMP("%s: enter %s %s", this, _name, _names);
      std::string name = filter_field_name(_name);
      auto* cur = _message_stack.back();
      auto* ref = cur->GetReflection();
      auto* desc = cur->GetDescriptor();
      if (_field)
      {
        // try to see if we mapped foo.bar onto foo_bar
        auto mapped = _names.back() + "_" + name;
        auto field = desc->FindFieldByName(mapped);
        if (!field)
          elle::err("_enter %s with _field set at %s", name, _names);
        _field = nullptr;
        _message_stack.push_back(cur);
        return _enter(mapped);
      }
      _field = desc->FindFieldByName(name);
      if (!_field)
        _field = desc->FindFieldByName(name + std::to_string(_last_serialized_int));
      if (!_field)
        elle::err("field %s does not exist in %s", name, desc->name());
      if (!_field->is_repeated()
        && _field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE
        && !ref->HasField(*cur, _field))
      { // HasField returns false on primitive fields with default value (empty string)
        ELLE_LOG("missing key %s", name);
        throw elle::serialization::MissingKey(name);
      }
      if (!_field->is_repeated() && _field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE)
      {
        auto* child = &ref->GetMessage(*cur, _field);
        _message_stack.push_back(child);
        _field = nullptr;
      }
      return true;
    }

    void
    SerializerIn::_leave(std::string const& name)
    {
      ELLE_DUMP("%s: leave %s %s", this, name, _names);
      if (_field != nullptr)
        _field = nullptr;
      else
        _message_stack.pop_back();
    }

    void
    SerializerIn::_serialize(std::string& v)
    {
      ELLE_ASSERT(_field);
      if (_field->type() != google::protobuf::FieldDescriptor::TYPE_STRING
        && _field->type() != google::protobuf::FieldDescriptor::TYPE_BYTES)
        elle::err<elle::serialization::Error>(
          "field %s is of type %s not string", _field->name(), _field->type());
      auto* cur = _message_stack.back();
      std::string str;
      if (_field->is_repeated())
        str = cur->GetReflection()->GetRepeatedString(*cur, _field, _index);
      else
        str = cur->GetReflection()->GetString(*cur, _field);
      v = std::move(str);
      ELLE_DUMP("deserialized string '%s'", v);
    }

    void
    SerializerIn::_serialize(elle::Buffer& b)
    {
      std::string s;
      _serialize(s);
      b = s;
    }

    template<typename T, typename E>
    T get_small_int(E v)
    {
      if (v > std::numeric_limits<T>::max())
        elle::err<elle::serialization::Error>("%s does not fit in %s bytes", v, sizeof(T));
      if (v < std::numeric_limits<T>::min())
        elle::err<elle::serialization::Error>("underflow: %s does not fit in %s bytes or unsigned", v, sizeof(T));
      return static_cast<T>(v);
    }

    template<typename T>
    void
    SerializerIn::_serialize_int(T& v)
    {
      ELLE_ASSERT(_field);
      if (_field->type() == google::protobuf::FieldDescriptor::TYPE_STRING)
        elle::err("got a string where an int was expected");
      auto* cur = _message_stack.back();
      {
        // FIXME will this call pass for other int types
        int64_t bigval;
        if (_field->is_repeated())
          bigval = cur->GetReflection()->GetRepeatedInt64(*cur, _field, _index);
        else
          bigval = cur->GetReflection()->GetInt64(*cur, _field);
        v = get_small_int<T>(bigval);
        _last_serialized_int = v;
      }
      ELLE_DUMP("deserialized int: %s", v);
    }

    void SerializerIn::_serialize(int8_t  & v) { _serialize_int(v);}
    void SerializerIn::_serialize(uint8_t & v) { _serialize_int(v);}
    void SerializerIn::_serialize(int16_t & v) { _serialize_int(v);}
    void SerializerIn::_serialize(uint16_t& v) { _serialize_int(v);}
    void SerializerIn::_serialize(int32_t & v) { _serialize_int(v);}
    void SerializerIn::_serialize(uint32_t& v) { _serialize_int(v);}
    void SerializerIn::_serialize(int64_t & v) { _serialize_int(v);}

    void
    SerializerIn::_serialize(uint64_t& v)
    {
      ELLE_ASSERT(_field);
      auto* cur = _message_stack.back();
      uint64_t bigval;
      if (_field->is_repeated())
        bigval = cur->GetReflection()->GetRepeatedUInt64(*cur, _field, _index);
      else
        bigval = cur->GetReflection()->GetUInt64(*cur, _field);
      v = get_small_int<uint64_t>(bigval);
      _last_serialized_int = v;
    }

    void
    SerializerIn::_serialize(double& v)
    {
      ELLE_ASSERT(_field);
      auto* cur = _message_stack.back();
      if (_field->is_repeated())
        v = cur->GetReflection()->GetRepeatedDouble(*cur, _field, _index);
      else
        v = cur->GetReflection()->GetDouble(*cur, _field);
    }

    void
    SerializerIn::_serialize(boost::posix_time::ptime& v)
    {
      std::string s;
      _serialize(s);
      v = boost::posix_time::from_iso_string(s);
    }

    void
    SerializerIn::_serialize(bool& b)
    {
      ELLE_ASSERT(_field);
      auto* cur = _message_stack.back();
      if (_field->is_repeated())
        b = cur->GetReflection()->GetRepeatedBool(*cur, _field, _index);
      else
        b = cur->GetReflection()->GetBool(*cur, _field);
    }

    void
    SerializerIn::_serialize_time_duration(std::int64_t& ticks,
                                           std::int64_t& num,
                                           std::int64_t& denom)
    {
      elle::err("not implemented");
    }

    void
    SerializerIn::_serialize_named_option(std::string const& name,
                                          bool present,
                                          std::function<void ()> const& f)
    {
      ELLE_DUMP("deserialize_named_option %s", name);
      ELLE_ASSERT(!_field);
      auto* cur = _message_stack.back();
      auto* ref = cur->GetReflection();
      auto* desc = cur->GetDescriptor();
      auto* field = desc->FindFieldByName(name);
      if (!field)
        return;
      if (!field->is_repeated() && !ref->HasField(*cur, field))
        return;
      f();
    }

    void
    SerializerIn::_serialize_option(bool present,
                                    std::function<void ()> const& f)
    {
      f();
    }

    void
    SerializerIn::_serialize_array(int, std::function<void ()> const& f)
    {
      ELLE_ASSERT(_field);
      auto* cur = _message_stack.back();
      auto* ref = cur->GetReflection();
      int count = ref->FieldSize(*cur, _field);
      int prev_index = _index;
      auto prev_field = _field;
      elle::SafeFinally restore_index([&] {
          _index = prev_index;
          _field = prev_field;
      });
      std::string name;
      if (!_names.empty())
        name = _names.back();
      ELLE_DUMP("%s: serialize_array count=%s name=%s object=%s", this, count,
        _names.empty()? std::string("none") : _names.back(),
        _field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE
        );
      if (_field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE)
      {
        //if (!name.empty())
        //  _leave(name);
        for (_index = 0; _index < count; ++_index)
        {
          f();
        }
        //if (!name.empty())
        //  _enter(name);
      }
      else
      {
        _field = nullptr;
        for (_index = 0; _index < count; ++_index)
        {
          auto const* next = &ref->GetRepeatedMessage(*cur, prev_field, _index);
          _message_stack.push_back(next);
          f();
          _message_stack.pop_back();
        }
      }
    }

    void
    SerializerIn::_serialize_variant(std::vector<std::string> const& names,
                                     int index, // out: filled, in: -1
                                     std::function<void(int)> const& f)
    {
      ELLE_DUMP("deserializing variant at %s with %s", index, names);
      std::string name;
      this->serialize("type", name);
      ELLE_DUMP("got: %s", name);
      auto it = std::find(names.begin(), names.end(), name);
      if (it == names.end())
        elle::err<elle::serialization::Error>("type %s not found in %s", name, names);
      index = it - names.begin();
      name = cxx_to_message_name(name);
      this->_enter(name);
      elle::SafeFinally leave([&] { this->_leave(name);});
      f(index);
    }

    SerializerOut::SerializerOut(google::protobuf::Message* msg)
    : elle::serialization::SerializerOut(false)
    {
      _field = nullptr;
      _message_stack.push_back(msg);
      _array_handler.push_back(" IN.VALID ");
    }

    void
    SerializerOut::_field_check()
    { // grpc can't handle raw value as output, only messages
      // so if C++ outputs something that serializes as a value, we need
      // to wrap it.
      // Assume the field name is the lowercase version of the message name
      if (!_field && _message_stack.size() == 1)
      {
        auto* cur = _message_stack.back();
        auto* desc = cur->GetDescriptor();
        ELLE_DEBUG("field check for %s", desc->name());
        _field = desc->FindFieldByName(uppercase_to_underscore(desc->name()));
        if (!_field)
        { // Nice heuristic bearclaw, but the Message names are constrained
          // by protobuf standards, just take the first field
          _field = desc->FindFieldByNumber(1);
        }
      }
      ELLE_ASSERT(_field);
    }

    bool
    SerializerOut::_enter(std::string const& _name)
    {
      ELLE_DUMP("enter %s %s", _name, _names);
      std::string name = filter_field_name(_name);
      auto* cur = _message_stack.back();
      auto* ref = cur->GetReflection();
      auto* desc = cur->GetDescriptor();
      if (_field)
      {
        // try to see if we mapped foo.bar onto foo_bar
        auto mapped = _names.back() + "_" + name;
        auto field = desc->FindFieldByName(mapped);
        if (!field)
          elle::err("_enter %s with _field set at %s", name, _names);
        ELLE_DEBUG("remapping %s to %s at %s", name, mapped, _names);
        _field = nullptr;
        _message_stack.push_back(cur);
        return _enter(mapped);
      }

      _field = desc->FindFieldByName(name);
      if (!_field)
        _field = desc->FindFieldByName(name + std::to_string(_last_serialized_int));
      if (!_field)
        elle::err("field %s not found at %s in %s", name, _names, desc->name());
      if (_field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE)
      {
        google::protobuf::Message* child = nullptr;
        if (_field->is_repeated())
        {
          ELLE_DUMP("enter repeated n=%s ah=%s", _names, _array_handler);
          if (_array_handler == _names)
            child = ref->AddMessage(cur, _field);
          // else push a nullptr message to the stack, we will leave
          // before doing anything
        }
        else
          child = ref->MutableMessage(cur, _field);
        _message_stack.push_back(child);
        _field = nullptr;
      }
      return true;
    }

    void
    SerializerOut::_leave(std::string const& name)
    {
      ELLE_DUMP("leave %s %s", name, _names);
      if (_field != nullptr)
        _field = nullptr;
      else
        _message_stack.pop_back();
    }

    void
    SerializerOut::_serialize(std::string& v)
    {
      ELLE_DUMP("serializing string: '%s'", v);
      _field_check();
      if (_field->type() != google::protobuf::FieldDescriptor::TYPE_STRING
        &&_field->type() != google::protobuf::FieldDescriptor::TYPE_BYTES)
        elle::err<elle::serialization::Error>(
          "field %s is of type %s not string", _field->name(), _field->type());
      auto* cur = _message_stack.back();
      if (_field->is_repeated())
        cur->GetReflection()->AddString(cur, _field, v);
      else
        cur->GetReflection()->SetString(cur, _field, v);
    }

    void
    SerializerOut::_serialize(elle::Buffer& b)
    {
      std::string s = b.string();
      _serialize(s);
    }

    template<typename T>
    void
    SerializerOut::_serialize_int(T& v)
    {
      // FIXME: it is invalid to call SetInt64 on an INT32 field
      _field_check();
      auto* cur = _message_stack.back();
      if (_field->is_repeated())
        cur->GetReflection()->AddInt64(cur, _field, v);
      else
        cur->GetReflection()->SetInt64(cur, _field, v);
      _last_serialized_int = v;
    }

    void SerializerOut::_serialize(int8_t  & v) { _serialize_int(v);}
    void SerializerOut::_serialize(uint8_t & v) { _serialize_int(v);}
    void SerializerOut::_serialize(int16_t & v) { _serialize_int(v);}
    void SerializerOut::_serialize(uint16_t& v) { _serialize_int(v);}
    void SerializerOut::_serialize(int32_t & v) { _serialize_int(v);}
    void SerializerOut::_serialize(uint32_t& v) { _serialize_int(v);}
    void SerializerOut::_serialize(int64_t & v) { _serialize_int(v);}

    void
    SerializerOut::_serialize(uint64_t& v)
    {
      ELLE_ASSERT(_field);
      auto* cur = _message_stack.back();
      if (_field->is_repeated())
        cur->GetReflection()->AddUInt64(cur, _field, v);
      else
        cur->GetReflection()->SetUInt64(cur, _field, v);
      _last_serialized_int = v;
    }

    void
    SerializerOut::_serialize(double& v)
    {
      _field_check();
      auto* cur = _message_stack.back();
      if (_field->is_repeated())
        cur->GetReflection()->AddDouble(cur, _field, v);
      else
        cur->GetReflection()->SetDouble(cur, _field, v);
    }

    void
    SerializerOut::_serialize(boost::posix_time::ptime& v)
    {
      std::string s = boost::posix_time::to_iso_string(v);
      _serialize(s);
    }

    void
    SerializerOut::_serialize(bool& b)
    {
      _field_check();
      auto* cur = _message_stack.back();
      if (_field->is_repeated())
        cur->GetReflection()->AddBool(cur, _field, b);
      else
        cur->GetReflection()->SetBool(cur, _field, b);
    }

    void
    SerializerOut::_serialize_time_duration(std::int64_t& ticks,
                                           std::int64_t& num,
                                           std::int64_t& denom)
    {
      elle::err("not implemented");
    }

    void
    SerializerOut::_serialize_named_option(std::string const& name,
                                          bool present,
                                          std::function<void ()> const& f)
    {
      ELLE_DUMP("serialize_named_option %s", name);
      ELLE_ASSERT(!_field);
      //_enter(name);
      f();
    }

    void
    SerializerOut::_serialize_option(bool present,
                                    std::function<void ()> const& f)
    {
      if (present)
        f();
      else if (!_field)
      { // damm, we created a dummy default-initialized entry where we should not, remove it
        if (!_names.empty())
        {
          ELLE_ASSERT(_message_stack.size() >= 2);
          auto* parent = _message_stack[_message_stack.size()-2];
          auto* ref = parent->GetReflection();
          auto* desc = parent->GetDescriptor();
          auto* field = desc->FindFieldByName(_names.back());
          ref->ClearField(parent, field);
        }
      }
      else
      { // we still need to reset the field in case it was set in a prior use
        // of the message
        auto* cur = _message_stack.back();
        cur->GetReflection()->ClearField(cur, _field);
      }
    }

    void
    SerializerOut::_serialize_array(int count, std::function<void ()> const& f)
    {
      std::string name;
      if (!_names.empty())
        name = _names.back();
      ELLE_DUMP("serialize_array count=%s name=%s field=%s", count,
        _names.empty()? std::string("none") : _names.back(), !!_field);
      if (_field)
      {
        auto* cur = _message_stack.back();
        auto* ref = cur->GetReflection();
        ref->ClearField(cur, _field);
      }
      int prev_index = _index;
      auto prev_array_handler = _array_handler;
      elle::SafeFinally restore_index([&] {
          _index = prev_index;
      });
      // callback will call enter/leave for each array element
      if (!name.empty())
        _leave(name);
      _array_handler = _names;
      f();
      _array_handler = prev_array_handler;
      if (!name.empty())
        _enter(name);
    }

    void
    SerializerOut::_serialize_variant(std::vector<std::string> const& names,
                                     int index, // out: filled, in: -1
                                     std::function<void(int)> const& f)
    {
      ELLE_DUMP("serializing variant at %s with %s", index, names);
      ELLE_ASSERT_GT((signed)names.size(), index);
      std::string name = names[index];
      this->serialize("type", names[index]);
      name = cxx_to_message_name(name);
      this->_enter(name);
      elle::SafeFinally leave([&] { this->_leave(name);});
      f(index);
    }
  }
}
