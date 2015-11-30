/// Factored code used by infinit-device and infinit-user.

#include <elle/format/hexadecimal.hh>

using namespace boost::program_options;

void
echo_mode(bool enable)
{
#if defined(INFINIT_WINDOWS)
  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode;
  GetConsoleMode(hStdin, &mode);
  if (!enable)
    mode &= ~ENABLE_ECHO_INPUT;
  else
    mode |= ENABLE_ECHO_INPUT;
  SetConsoleMode(hStdin, mode );
#else
  struct termios tty;
  tcgetattr(STDIN_FILENO, &tty);
  if(!enable)
    tty.c_lflag &= ~ECHO;
  else
    tty.c_lflag |= ECHO;
  (void)tcsetattr(STDIN_FILENO, TCSANOW, &tty);
#endif
}

std::string
read_passphrase(std::string const& prompt_text = "Passphrase")
{
  std::string res;
  {
    elle::SafeFinally restore_echo([] { echo_mode(true); });
    echo_mode(false);
    std::cout << prompt_text << ": ";
    std::cout.flush();
    std::getline(std::cin, res);
  }
  std::cout << std::endl;
  return res;
}

static
std::string
_password(variables_map const& args,
          std::string const& argument)
{
  auto password = optional(args, argument);
  if (!password)
    password = read_passphrase("Password");
  ELLE_ASSERT(password);
  return password.get();
};

static
std::string
hash_password(std::string const& password_,
              std::string salt)
{
  auto password = password_ + salt;
  return elle::format::hexadecimal::encode(
    infinit::cryptography::hash(
      password, infinit::cryptography::Oneway::sha256).string()
    );
  return password;
};
