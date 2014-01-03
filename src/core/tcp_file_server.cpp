#include "core/tcp_file_server.h"

#include "core/array.h"
#include "core/free_list.h"
#include "core/static_array.h"
#include "core/tcp_file_device.h"
#include "core/task.h"
#include "core/tcp_acceptor.h"
#include "core/tcp_stream.h"
#include "core/os_file.h"

namespace Lux
{
	namespace FS
	{
		class TCPFileServerTask : public MT::Task
		{
		public:
			TCPFileServerTask() {}
			~TCPFileServerTask() {}

			int task()
			{
				bool quit = false;

				m_acceptor.start("127.0.0.1", 10001);
				Net::TCPStream* stream = m_acceptor.accept();

				OsFile* file = LUX_NEW(OsFile)();
				while(!quit)
				{
					int32_t op = 0;
					stream->read(op);
					switch(op)
					{
					case TCPCommand::OpenFile:
						{
							int32_t mode = 0;
							int32_t len = 0;
							stream->read(mode);
							stream->read(m_buffer.data(), m_buffer.size());

							int32_t ret = -2;
							int32_t id = m_ids.alloc();
							if(id > 0)
							{
								OsFile* file = LUX_NEW(OsFile)();
								m_files[id] = file;

								ret = file->open(m_buffer.data(), mode) ? id : -1;
							}
							stream->write(ret);
						}
						break;
					case TCPCommand::Close:
						{
							uint32_t id = -1;
							stream->read(id);
							OsFile* file = m_files[id];
							m_ids.release(id);

							file->close();
							delete file;
						}
						break;
					case TCPCommand::Read:
						{
							uint32_t id = -1;
							stream->read(id);
							OsFile* file = m_files[id];

							uint32_t size = 0;
							stream->read(size);

							while(size > 0)
							{
								int32_t read = size > m_buffer.size() ? m_buffer.size() : size;
								file->read((void*)m_buffer.data(), read);
								stream->write((const void*)m_buffer.data(), read);
								size -= read;
							}
						}
						break;
					case TCPCommand::Write:
						{
							uint32_t id = -1;
							stream->read(id);
							OsFile* file = m_files[id];

							uint32_t size = 0;
							stream->read(size);

							while(size > 0)
							{
								int32_t read = size > m_buffer.size() ? m_buffer.size() : size;
								stream->read((void*)m_buffer.data(), read);
								file->write(m_buffer.data(), read);
								size -= read;
							}
						}
						break;
					case TCPCommand::Size:
						{
							uint32_t id = -1;
							stream->read(id);
							OsFile* file = m_files[id];

							uint32_t size = file->size();
							stream->write(size);
						}
						break;
					case TCPCommand::Seek:
						{
							uint32_t id = -1;
							stream->read(id);
							OsFile* file = m_files[id];

							uint32_t base = 0;
							int32_t offset = 0;
							stream->read(base);
							stream->read(offset);
							
							uint32_t pos = file->seek((SeekMode)base, offset);
							stream->write(pos);
						}
						break;
					case TCPCommand::Pos:
						{
							uint32_t id = -1;
							stream->read(id);
							OsFile* file = m_files[id];

							uint32_t pos = file->pos();
							stream->write(pos);
						}
						break;
					case TCPCommand::Disconnect:
						{
							quit = true;
							break;
						}
					default:
						ASSERT(0);
						break;
					}
				}
				return 0;
			}

			void stop() {} // TODO: implement stop 

		private:
			Net::TCPAcceptor			m_acceptor;
			StaticArray<char, 0x50000>	m_buffer;
			StaticArray<OsFile*, 0x50000> m_files;
			FreeList<int32_t, 0x50000>	m_ids;
		};

		struct TCPFileServerImpl
		{
			TCPFileServerTask m_task;
		};

		TCPFileServer::TCPFileServer()
		{
			m_impl = NULL;
		}

		TCPFileServer::~TCPFileServer()
		{
			LUX_DELETE(m_impl);
		}

		void TCPFileServer::start()
		{
			m_impl = LUX_NEW(TCPFileServerImpl);
			m_impl->m_task.create("TCP File Server Task");
			m_impl->m_task.run();
		}

		void TCPFileServer::stop()
		{
			m_impl->m_task.stop();
			m_impl->m_task.destroy();
			LUX_DELETE(m_impl);
			m_impl = NULL;
		}
	} // ~namespace FS
} // ~namespace Lux