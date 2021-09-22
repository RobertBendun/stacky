#include <concepts>
#include <cstdio>
#include <ext/stdio_filebuf.h>
#include <istream>
#include <type_traits>
#include <exception>

#include <unistd.h>
#include <paths.h>
#include <sys/types.h>
#include <sys/wait.h>

namespace rp
{
	template<typename T>
	concept CStringable = requires (T const& a) {
		{ a.c_str() } -> std::convertible_to<char const*>;
	};

  enum class captured_stream : decltype(STDOUT_FILENO)
  {
    default_output = STDOUT_FILENO,
    error_output   = STDERR_FILENO
  };

	struct Exit_Code_Exception : std::exception
	{
		virtual ~Exit_Code_Exception() = default;

		explicit Exit_Code_Exception(int exit_code, std::string_view type) : exit_code(exit_code)
		{
			std::stringstream ss;
			ss << "Program execution was " << type << " with exit code = " << exit_code;
			message = std::move(ss).str();
		}

		virtual char const* what() const noexcept override
		{
			return message.c_str();
		}

		int exit_code;
		std::string message;
	};

  namespace ipstream_details
  {
    template<typename CharT, typename Traits, typename = typename std::enable_if<std::is_same<char, CharT>::value>::type>
    __gnu_cxx::stdio_filebuf<CharT, Traits> open_process(const CharT *program, ::pid_t &pid, captured_stream fdOfChild)
    {
      int pipe[2];
      char *args[] = { const_cast<char*>("sh"), const_cast<char*>("-c"), nullptr, nullptr };

      if (::pipe(pipe) < 0) {
        return {};
      }

      if ((pid = ::fork()) < 0) {
        ::close(pipe[0]);
        ::close(pipe[1]);
        return {};
      }
      else if (pid == 0) {
        ::close(pipe[0]);
        if (pipe[1] != static_cast<int>(fdOfChild)) {
          ::dup2(pipe[1], static_cast<int>(fdOfChild));
          ::close(pipe[1]);
        }

        fclose(freopen(_PATH_DEVNULL, "w", fdOfChild == captured_stream::default_output ? stderr : stdout));

        args[2] = (char *)program;
        ::execv(_PATH_BSHELL, args);
        ::_exit(1);
      }

      ::close(pipe[1]);
      return __gnu_cxx::stdio_filebuf<CharT, Traits>(pipe[0], std::ios_base::in);
    }

    template<typename CharT, typename Traits>
    void close_process(__gnu_cxx::stdio_filebuf<CharT, Traits> &filebuf, pid_t pid)
    {
      int status;
      assert(::waitpid(pid, &status, 0) != -1);
      filebuf.close();

			if (WIFEXITED(status)) {
				if (auto exit_code = WEXITSTATUS(status); exit_code != 0)
					throw Exit_Code_Exception(exit_code, "exited");
			} else if (WIFSIGNALED(status)) {
				throw Exit_Code_Exception(WTERMSIG(status), "signaled");
			} else if (WIFSTOPPED(status)) {
				throw Exit_Code_Exception(WSTOPSIG(status), "stopped");
			}
    }
  }


  template<typename CharT, typename Traits = std::char_traits<CharT>>
  struct basic_ipstream : std::basic_istream<CharT, Traits>
  {
    basic_ipstream() : Base(&filebuf), filebuf() {}

    basic_ipstream(const CharT *program, captured_stream stream = captured_stream::default_output)
      : Base(&filebuf), filebuf(::rp::ipstream_details::open_process<CharT, Traits>(program, pid, stream))
    {
    }

    basic_ipstream(CStringable auto const &program, captured_stream stream = captured_stream::default_output)
      : Base(&filebuf), filebuf(::rp::ipstream_details::open_process<CharT, Traits>(program.c_str(), pid, stream))
    {
    }

    ~basic_ipstream() noexcept
    {
      close();
    }

    bool is_open() const
    {
      return filebuf.is_open();
    }

    void open(const CharT *program, captured_stream stream = captured_stream::default_output)
    {
      filebuf = ::rp::ipstream_details::open_process<CharT, Traits>(program, pid, stream);
    }

    void open(const std::basic_string<CharT> &program, captured_stream stream = captured_stream::default_output)
    {
      filebuf = ::rp::ipstream_details::open_process<CharT, Traits>(program.c_str(), pid, stream);
    }

    __gnu_cxx::stdio_filebuf<CharT, Traits>* rdbuf()
    {
      return &filebuf;
    }

    __gnu_cxx::stdio_filebuf<CharT, Traits> const* rdbuf() const
    {
      return &filebuf;
    }

    void close()
    {
      ::rp::ipstream_details::close_process(filebuf, pid);
      if (filebuf.is_open())
        filebuf.close();
    }

  private:
    using Base = std::basic_istream<CharT, Traits>;
    __gnu_cxx::stdio_filebuf<CharT, Traits> filebuf;
    pid_t pid = -1;
    unsigned capturedFd = STDOUT_FILENO;
  };

  using ipstream = basic_ipstream<char>;
  using wipstream = basic_ipstream<wchar_t>; // ! currently not supported by open_process function
}
