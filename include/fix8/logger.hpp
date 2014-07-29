//-------------------------------------------------------------------------------------------------
/*

Fix8 is released under the GNU LESSER GENERAL PUBLIC LICENSE Version 3.

Fix8 Open Source FIX Engine.
Copyright (C) 2010-14 David L. Dight <fix@fix8.org>

Fix8 is free software: you can  redistribute it and / or modify  it under the  terms of the
GNU Lesser General  Public License as  published  by the Free  Software Foundation,  either
version 3 of the License, or (at your option) any later version.

Fix8 is distributed in the hope  that it will be useful, but WITHOUT ANY WARRANTY;  without
even the  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

You should  have received a copy of the GNU Lesser General Public  License along with Fix8.
If not, see <http://www.gnu.org/licenses/>.

BECAUSE THE PROGRAM IS  LICENSED FREE OF  CHARGE, THERE IS NO  WARRANTY FOR THE PROGRAM, TO
THE EXTENT  PERMITTED  BY  APPLICABLE  LAW.  EXCEPT WHEN  OTHERWISE  STATED IN  WRITING THE
COPYRIGHT HOLDERS AND/OR OTHER PARTIES  PROVIDE THE PROGRAM "AS IS" WITHOUT WARRANTY OF ANY
KIND,  EITHER EXPRESSED   OR   IMPLIED,  INCLUDING,  BUT   NOT  LIMITED   TO,  THE  IMPLIED
WARRANTIES  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  THE ENTIRE RISK AS TO
THE QUALITY AND PERFORMANCE OF THE PROGRAM IS WITH YOU. SHOULD THE PROGRAM PROVE DEFECTIVE,
YOU ASSUME THE COST OF ALL NECESSARY SERVICING, REPAIR OR CORRECTION.

IN NO EVENT UNLESS REQUIRED  BY APPLICABLE LAW  OR AGREED TO IN  WRITING WILL ANY COPYRIGHT
HOLDER, OR  ANY OTHER PARTY  WHO MAY MODIFY  AND/OR REDISTRIBUTE  THE PROGRAM AS  PERMITTED
ABOVE,  BE  LIABLE  TO  YOU  FOR  DAMAGES,  INCLUDING  ANY  GENERAL, SPECIAL, INCIDENTAL OR
CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OR INABILITY TO USE THE PROGRAM (INCLUDING BUT
NOT LIMITED TO LOSS OF DATA OR DATA BEING RENDERED INACCURATE OR LOSSES SUSTAINED BY YOU OR
THIRD PARTIES OR A FAILURE OF THE PROGRAM TO OPERATE WITH ANY OTHER PROGRAMS), EVEN IF SUCH
HOLDER OR OTHER PARTY HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.

*/
//-------------------------------------------------------------------------------------------------
#ifndef FIX8_LOGGER_HPP_
#define FIX8_LOGGER_HPP_

//-------------------------------------------------------------------------------------------------
#include <list>
#include <Poco/Net/IPAddress.h>
#include <Poco/Net/DatagramSocket.h>

//-------------------------------------------------------------------------------------------------
namespace FIX8 {

//-------------------------------------------------------------------------------------------------
/// File descriptor output streambuf, inspiration from Josuttis N.M.
class fdoutbuf : public std::streambuf
{
protected:
   int fd;

   virtual int_type overflow(int_type c)
   {
      if (c != traits_type::eof())
      {
         char z(c);
         if (write(fd, &z, 1) != 1)
            return traits_type::eof();
      }
      return c;
   }

   virtual std::streamsize xsputn(const char *s, std::streamsize num)
   {
      return write (fd, s, num);
   }

public:
   fdoutbuf(int _fd) : fd(_fd) {}
   virtual ~fdoutbuf () {}
};

/// File pointer stream
class fptrostream : public std::ostream
{
   FILE *fptr_;

protected:
   fdoutbuf buf_;

public:
	/*! Ctor.
	    \param fptr FILE* */
   fptrostream(FILE *fptr)
		: std::ostream(&buf_), fptr_(fptr), buf_(fileno(fptr)) {}

	/// Dtor.
   virtual ~fptrostream ()
   {
#ifdef _MSC_VER
	   _pclose(fptr_);
#else
	   pclose(fptr_);
#endif
   }

	/*! Get the filno (fd)
	    \return fd */
   int getfileno() { return fileno(fptr_); }
};

//-------------------------------------------------------------------------------------------------
/// Socket output streambuf, inspiration from Josuttis N.M.
class bcoutbuf : public std::streambuf
{
protected:
	Poco::Net::DatagramSocket *_sock;

   virtual int_type overflow(int_type c)
   {
      if (c != traits_type::eof())
      {
         char z(c);
         _sock->sendBytes(&z, 1);
      }
      return c;
   }

   virtual std::streamsize xsputn(const char *s, std::streamsize num)
   {
      _sock->sendBytes(s, num);
      return num;
   }

public:
   bcoutbuf(Poco::Net::DatagramSocket *sock) : _sock(sock) {}
   virtual ~bcoutbuf() { _sock->close(); delete _sock; }
};

/// udp stream
class bcostream : public std::ostream
{
protected:
   bcoutbuf buf_;

public:
	/*! Ctor.
	    \param sock datagram socket */
   bcostream(Poco::Net::DatagramSocket *sock) : std::ostream(&buf_), buf_(sock) {}

	/// Dtor.
   virtual ~bcostream() {}
};

//-------------------------------------------------------------------------------------------------
class Tickval;

//-------------------------------------------------------------------------------------------------
/// f8_thread delegated async logging class
class Logger
{
	f8_thread<Logger> _thread;
	std::list<std::string> _buffer;

public:
	enum Flags { append, timestamp, sequence, compress, pipe, broadcast, thread, direction,
					 buffer, inbound, outbound, nolf, minitimestamp, num_flags };
	enum { rotation_default = 5, max_rotation = 64};
	using LogFlags = ebitset<Flags>;

protected:
	f8_mutex _mutex;
	LogFlags _flags;
	std::ostream *_ofs = nullptr;
	size_t _lines = 0;
	f8_thread_cancellation_token _stopping;

	struct LogElement
	{
		thread_id_t _tid;
		std::string _str;
		unsigned _val;
		Tickval _when;

		LogElement(const thread_id_t tid, const std::string& str, const unsigned val=0)
			: _tid(tid), _str(str), _val(val), _when(true) {}
		LogElement() : _tid(), _val(), _when(true) {}
		LogElement(const LogElement& from) : _tid(from._tid), _str(from._str), _val(from._val), _when(from._when) {}
		LogElement& operator=(const LogElement& that)
		{
			if (this != &that)
			{
				_tid = that._tid;
				_str = that._str;
				_val = that._val;
				_when = that._when;
			}
			return *this;
		}
	};

	f8_concurrent_queue<LogElement> _msg_queue;
	unsigned _sequence = 0, _osequence = 0;

	using ThreadCodes = std::map<thread_id_t, char>;
	ThreadCodes _thread_codes;

	using RevThreadCodes = std::map<char, thread_id_t>;
	RevThreadCodes _rev_thread_codes;

public:
	/*! Ctor.
	    \param flags ebitset flags */
	Logger(const LogFlags flags) : _thread(std::ref(*this)), _flags(flags)
	{
		_thread.start();
	}

	/// Dtor.
	virtual ~Logger()
	{
		stop();
		delete _ofs;
	}

	/*! Set the LogFlags
	    \param flags flags to set */
	void set_flags(LogFlags flags) { _flags = flags; }

	/*! Get the underlying stream object.
	    \return the stream */
	virtual std::ostream& get_stream() const { return _ofs ? *_ofs : std::cout; }

	/*! Log a string.
	    \param what the string to log
	    \param val optional value for the logger to use
	    \return true on success */
	bool send(const std::string& what, const unsigned val=0)
	{
		const LogElement le(f8_thread<Logger>::getid(), what, val);
		return _msg_queue.try_push (le) == 0;
	}

	/// Stop the logging thread.
	void stop() { _stopping.request_stop(); send(std::string()); _thread.join(); }

	/*! Perform logfile rotation. Only relevant for file-type loggers.
		\param force the rotation (even if the file is set to append)
	   \return true on success */
	virtual bool rotate(bool force=false) { return true; }

	/*! The logging thread entry point.
	    \return 0 on success */
	F8API int operator()();

	/// string representation of logflags
	static const std::vector<std::string> _bit_names;

	/*! Check if the given log flag is set
		\param flg flag bit to check
		\return true if set */
	bool has_flag(const Flags flg) const { return _flags.has(flg); }

	/*! Get the thread code for this thread or allocate a new code if not found.
		\param tid the thread id of the thread to get a code for */
	F8API char get_thread_code(thread_id_t tid);

	/// Remove dead threads from the thread code cache.
	F8API void purge_thread_codes();

	/// Flush the buffer
	F8API virtual void flush();

	/*! Get the thread cancellation token
		\return reference to the cancellation token */
	f8_thread_cancellation_token& cancellation_token() { return _stopping; }
};

//-------------------------------------------------------------------------------------------------
/// A file logger.
class FileLogger : public Logger
{
	std::string _pathname;
	unsigned _rotnum;

public:
	/*! Ctor.
	    \param pathname pathname to log to
	    \param flags ebitset flags
	    \param rotnum number of logfile rotations to retain (default=5) */
	F8API FileLogger(const std::string& pathname, const LogFlags flags, const unsigned rotnum=rotation_default);

	/// Dtor.
	virtual ~FileLogger() {}

	/*! Perform logfile rotation
	    \param force force the rotation (even if the file is set ti append)
	    \return true on success */
	F8API virtual bool rotate(bool force=false);
};

//-------------------------------------------------------------------------------------------------
/// A pipe logger.
class PipeLogger : public Logger
{
public:
	/*! Ctor.
	    \param command pipe command
	    \param flags ebitset flags */
	PipeLogger(const std::string& command, const LogFlags flags);

	/// Dtor.
	virtual ~PipeLogger() {}
};

//-------------------------------------------------------------------------------------------------
/// A broadcast logger.
class BCLogger : public Logger
{
	bool _init_ok;

public:
	/*! Ctor.
	    \param sock udp socket
	    \param flags ebitset flags */
	BCLogger(Poco::Net::DatagramSocket *sock, const LogFlags flags);

	/*! Ctor.
	    \param ip ip string
	    \param port port to use
	    \param flags ebitset flags */
	BCLogger(const std::string& ip, const unsigned port, const LogFlags flags);

	/*! Check to see if a socket was successfully created.
	  \return non-zero if ok, 0 if not ok */
	operator void*() { return _init_ok ? this : 0; }

	/// Dtor.
	virtual ~BCLogger() {}
};

//-------------------------------------------------------------------------------------------------
const size_t max_global_filename_length(1024);

/// A global singleton logger
/*! \tparam fn actual pathname of logfile
    \details Create a static instance of this template and set the template parameter to the desired logfile pathname */
template<char *fn>
class SingleLogger : public Singleton<SingleLogger<fn>>, public FileLogger
{
public:
	/// Ctor.
	SingleLogger() : FileLogger(fn, LogFlags() << timestamp << sequence << thread
#if defined BUFFERED_GLOBAL_LOGGING
		<< buffer
#endif
	) {}

	/*! Ctor with logflags
	   \param flags name to use */
	SingleLogger(LogFlags flags) : FileLogger(fn, flags) {}

	/*! Set the global logfile name.
	    \param from name to set to */
	static void set_global_filename(const std::string& from)
		{ CopyString(from, fn, max_global_filename_length); }

	/*! Send a message to the logger.
	  \param what message to log
	  \return true on success */
	static bool log(const std::string& what)
		{ return Singleton<SingleLogger<fn>>::instance()->send(what); }

	/*! Flush the logger */
	static void flush_log()
		{ Singleton<SingleLogger<fn>>::instance()->flush(); }

	/*! Set the logflags
	  \param flags flags to set */
	static void set_flags(LogFlags flags)
		{ Singleton<SingleLogger<fn>>::instance()->set_flags(flags); }

	/*! Stop the logger */
	static void stop()
		{ Singleton<SingleLogger<fn>>::instance()->stop(); }
};

//-----------------------------------------------------------------------------------------
class buffered_ostream : public std::ostream
{
protected:
	class tsbuf : public std::streambuf
	{
		std::string _str;

		int_type overflow(int_type c)
		{
			if (c != traits_type::eof())
			{
				char z(c);
				_str.append(&z, 1);
			}
			return c;
		}

		std::streamsize xsputn(const char *s, std::streamsize num)
			{ _str.append(s, num); return num; }

	public:
		tsbuf() = default;
		~tsbuf() = default;
		const std::string& get() const { return _str; }
	};

	tsbuf _buf;

public:
   buffered_ostream() : std::ostream(&_buf) {}
   virtual ~buffered_ostream() {}
};

//-----------------------------------------------------------------------------------------
using bool_func_string = std::function<bool(const std::string&)>;
using bool_func_string_int = std::function<bool(const std::string&, const unsigned)>;

class log_stream : public buffered_ostream
{
	bool_func_string _logger;

public:
	log_stream(decltype(_logger) func) : _logger(func) {}
	~log_stream() { _logger(_buf.get()); }
};

class log2_stream : public buffered_ostream
{
	bool_func_string_int _logger;

public:
	log2_stream(decltype(_logger) func) : _logger(func) {}
	~log2_stream() { _logger(_buf.get(), 0); }
};

//-----------------------------------------------------------------------------------------
F8API extern char glob_log0[max_global_filename_length];
using GlobalLogger = SingleLogger<glob_log0>;

// our buffered RAII ostream singleton insertable log target, global ostream log target
#define glout log_stream(bool_func_string(GlobalLogger::log))

//-------------------------------------------------------------------------------------------------

} // FIX8

#endif // _FIX8_LOGGER_HPP_
