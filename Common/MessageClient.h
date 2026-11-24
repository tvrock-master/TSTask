// Modified in this TSTask fork (recorded 2026-11-19).
#ifndef TSTASK_MESSAGE_CLIENT_H
#define TSTASK_MESSAGE_CLIENT_H


#include <functional>
#include "Message.h"


namespace TSTask
{

	class CMessageClient
	{
	public:
		CMessageClient();
		virtual ~CMessageClient();
		bool SendMessage(const CMessage *pSendMessage,CMessage *pReceiveMessage=nullptr);
		bool SetTimeout(DWORD Timeout);

	protected:
		String m_ServerName;
		DWORD m_Timeout;

		virtual bool Send(const void *pSendData,size_t SendSize,CDataBlock *pReceiveData) = 0;
	};

	class CLocalMessageClient : public CMessageClient
	{
	public:
		CLocalMessageClient();
		~CLocalMessageClient();
		bool SetServer(const String &ServerName,std::function<bool()> IsServerEnding={});

	private:
		std::function<bool()> m_IsServerEnding;
		bool Send(const void *pSendData,size_t SendSize,CDataBlock *pReceiveData) override;
		bool ReadPipe(HANDLE hPipe,void *pBuffer,DWORD Size,DWORD *pReadSize,OVERLAPPED *pOverlapped);
		bool ReadPipe(HANDLE hPipe,void *pBuffer,DWORD Size,OVERLAPPED *pOverlapped);
		bool WritePipe(HANDLE hPipe,const void *pBuffer,DWORD Size,DWORD *pWrittenSize,OVERLAPPED *pOverlapped);
		bool WritePipe(HANDLE hPipe,const void *pBuffer,DWORD Size,OVERLAPPED *pOverlapped);
	};

}


#endif
