#ifndef INFINIT_FILESYSTEM_FILEHANDLE_HH
# define INFINIT_FILESYSTEM_FILEHANDLE_HH

# include <infinit/filesystem/umbrella.hh>
# include <infinit/filesystem/File.hh>

namespace infinit
{
  namespace filesystem
  {
    class FileHandle
      : public rfs::Handle
      , public elle::Printable
    {
    public:
      FileHandle(std::shared_ptr<File> owner,
                 bool writable,
                 bool update_folder_mtime=false,
                 bool no_prefetch = false,
                 bool mark_dirty = false);
      ~FileHandle();
      virtual
      int
      read(elle::WeakBuffer buffer, size_t size, off_t offset) override;
      virtual
      int
      write(elle::WeakBuffer buffer, size_t size, off_t offset) override;
      virtual
      void
      ftruncate(off_t offset) override;
      virtual
      void
      fsync(int datasync) override;
      virtual
      void
      fsyncdir(int datasync) override;
      virtual
      void
      close() override;
      virtual
      void
      print(std::ostream& stream) const override;
    private:
      int
      _write_single(elle::WeakBuffer buffer, off_t offset);
      int
      _write_multi_single(elle::WeakBuffer buffer, off_t offset, int block);
      int
      _write_multi_multi(elle::WeakBuffer buffer, off_t offset,
                         int start_block, int end_block);
      ELLE_ATTRIBUTE(std::shared_ptr<File>, owner);
      ELLE_ATTRIBUTE(bool, dirty);
      ELLE_ATTRIBUTE(bool, writable);
    };

  }
}

#endif
