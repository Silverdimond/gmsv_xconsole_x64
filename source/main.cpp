
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>


#include <cstring>
#include <cstdint>
#include <string>
#include <thread>
#include <chrono>

#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/FactoryLoader.hpp>
#include <ByteBuffer.hpp>
#include <Platform.hpp>
#include <color.h>
#include <eiface.h>
#include <tier0/dbg.h>

#if ARCHITECTURE_IS_X86_64
#include <logging.h>
#endif


const char* EOL_SEQUENCE = "<EOL>\0";
const char* PIPE_NAME_OUT = "/tmp/garrysmod_console";
const char* PIPE_NAME_IN = "/tmp/garrysmod_console_in";

static int serverPipe = -1;
static int serverPipeIn = -1;


static volatile bool serverShutdown = false;
static volatile bool serverConnected = false;
static std::thread serverThread;

#if ARCHITECTURE_IS_X86_64
class XConsoleListener : public ILoggingListener
{
public:
	XConsoleListener(bool bQuietPrintf = false, bool bQuietDebugger = false) {}

	void Log(const LoggingContext_t* pContext, const char* pMessage) override
	{
		const CLoggingSystem::LoggingChannel_t* chan = LoggingSystem_GetChannel(pContext->m_ChannelID);
		const Color* color = &pContext->m_Color;
		int size = 12 + std::strlen(chan->m_Name) + std::strlen(pMessage);
		MultiLibrary::ByteBuffer buffer;
		buffer.Reserve(size);

		buffer <<
			static_cast<int32_t>(chan->m_ID) <<
			pContext->m_Severity <<
			chan->m_Name <<
			color->GetRawColor() <<
			pMessage;


        buffer << "<EOL>";

     ///   if (serverPipe == -1)
        //{
          //  serverConnected = false;
           // return;
        //}
const char* testMsg = "Test write to log from XConsole\n";
write(serverPipe, testMsg, strlen(testMsg));
ssize_t result = write(serverPipe, buffer.GetBuffer(), buffer.Size());
if (result == -1) {
} else {
    fsync(serverPipe); 
}
        ssize_t result = write(serverPipe, buffer.GetBuffer(), buffer.Size());
        if (result == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                serverConnected = false; 
            }
        } 
    }
};

ILoggingListener* listener = new XConsoleListener();
#else
static SpewOutputFunc_t spewFunction = nullptr;
static SpewRetval_t EngineSpewReceiver(SpewType_t type, const char* msg)
{
	if (!serverConnected)
		return spewFunction(type, msg);

	const Color* color = GetSpewOutputColor();
	MultiLibrary::ByteBuffer buffer;
	buffer.Reserve(512);


	buffer <<
		static_cast<int32_t>(type) <<
		GetSpewOutputLevel() <<
		GetSpewOutputGroup() <<
		color->GetRawColor() <<
		msg <<
		"<EOL>";

	if (serverPipe == -1)
	{
		serverConnected = false;
		return spewFunction(type, msg);
	}
 ssize_t result = write(serverPipe, buffer.GetBuffer(), buffer.Size());
    if (result == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            serverConnected = false; 
        }
    } 
    return spewFunction(type, msg);
}
#endif

static void RunCommand(std::string cmd)
{
	// in case the command hasnt been passed with a newline
	if (cmd[cmd.length() - 1] != '\n')
		cmd.append("\n");

	SourceSDK::FactoryLoader engine_loader("engine");
	IVEngineServer* engine_server = engine_loader.GetInterface<IVEngineServer>(INTERFACEVERSION_VENGINESERVER);
	engine_server->ServerCommand(cmd.c_str());
}


static void ServerThread()
{
	std::vector<uint8_t> dataBuffer(255);
	std::vector<uint8_t> buffer(255);

	int eolIndex = 0;
	while (!serverShutdown && serverPipeIn != -1)
	{
		int bytesRead = read(serverPipeIn, buffer.data(), buffer.size() - 1);
		if (bytesRead > 0)
		{
			for (int i = 0; i < bytesRead; i++)
			{
				uint8_t currentByte = buffer[i];
				dataBuffer.push_back(currentByte);

				// Check if the current byte matches the next byte in the EOL sequence
				if (currentByte == EOL_SEQUENCE[eolIndex])
				{
					eolIndex++;
					if (eolIndex == std::strlen(EOL_SEQUENCE)) // Full EOL found
					{
						dataBuffer.erase(dataBuffer.end() - eolIndex, dataBuffer.end());

						std::string cmd(dataBuffer.begin(), dataBuffer.end());
						RunCommand(cmd);

						dataBuffer.clear();
						eolIndex = 0;
					}
				}
				else
				{
					eolIndex = 0;
				}
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

static int CreateNamedPipe(GarrysMod::Lua::ILuaBase *LUA, const char* pipeName, int flags)
{
    struct stat sb;
    if (stat(pipeName, &sb) == 0) {
        unlink(pipeName);
    }

    mkfifo(pipeName, 0666);
    int pipe = open(pipeName, flags | O_NONBLOCK); 
    
    return pipe;
}
static int CreateLogFile(const char* fileName)
{
    int fd = open(fileName, O_WRONLY | O_CREAT | O_APPEND, 0666); 
    
    return fd;
}
GMOD_MODULE_OPEN()
{

serverPipe = CreateLogFile(PIPE_NAME_OUT);
serverConnected = (serverPipe != -1);
const char* testMsg = "Test write to log from open\n";
write(serverPipe, testMsg, strlen(testMsg));
ssize_t result = write(serverPipe, buffer.GetBuffer(), buffer.Size());
if (result == -1) {
} else {
    fsync(serverPipe); 
}
serverPipeIn = CreateNamedPipe(LUA, PIPE_NAME_IN, O_RDONLY); // Input ONLY
	serverThread = std::thread(ServerThread);

#if ARCHITECTURE_IS_X86_64
//LUA->ThrowError( "x86_64" );
	LoggingSystem_PushLoggingState(false, false);
	LoggingSystem_RegisterLoggingListener(listener);
#else
//LUA->ThrowError( "x86" );
	spewFunction = GetSpewOutputFunc();
	SpewOutputFunc(EngineSpewReceiver);
#endif

	return 0;
}

GMOD_MODULE_CLOSE()
{
#if ARCHITECTURE_IS_X86_64
	LoggingSystem_UnregisterLoggingListener(listener);
	LoggingSystem_PopLoggingState(false);
	delete listener;
#else
	SpewOutputFunc(spewFunction);
#endif

	serverShutdown = true;
	serverThread.join();

	close(serverPipe);
	//unlink(PIPE_NAME_OUT);

	close(serverPipeIn);
	unlink(PIPE_NAME_IN);

	return 0;
}
