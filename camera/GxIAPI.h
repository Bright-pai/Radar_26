

#ifndef GX_GALAXY_H
#define GX_GALAXY_H

#include "GXDef.h"
#include "GXErrorList.h"
#include "GxIAPILegacy.h"
#include "GxPixelFormat.h"

//------------------------------------------------------------------------------
//Chinese	��׼C API���ܺ�������
//English	Standard C API function definition
//------------------------------------------------------------------------------

#ifndef _WIN32
//----------------------------------------------------------------------------------
/**
\Chinese:
\brief		��ȡ��̬��汾��.
\return 	��̬��汾��.

\English
\brief      Gets the library version number.
\return     const char*   Library version number of string type.
*/
//----------------------------------------------------------------------------------
GX_EXTC GX_DLLEXPORT const char *GX_STDC GXGetLibVersion();
#endif

//------------------------------------------------------------------------
/**
\Chinese��
\brief		��ʼ���豸�⡣
\attention	���������ӿڣ�����GXGetLastError��GXCloseLib��֮ǰ�����ȵ��ô˽ӿڣ����û�����ʹ�ÿ��ʱ�����GXCloseLib�ͷſ���Դ��
			���֮ǰ�û��Ѿ����ù�GXInitLib��û�е���GXCloseLib�����ٴε���GXInitLib�ӿڣ��ӿڷ��سɹ���
\return		GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_FOUND_TL			�Ҳ���TL��
			�������������μ�GX_STATUS_LIST

\English:
\brief		Initialize the device library.
\attention	This interface must be called before calling any other interface (except GXGetLastError and GXCloseLib),
			and GXCloseLib is called to release library resources when the user is no longer using the library.
			If the user has called GXInitLib before, does not call GXCloseLib, and calls the GXInitLib interface again, the interface returns success.
\return		GX_STATUS_SUCCESS				The operation is successful, no error occurs.
			GX_STATUS_NOT_FOUND_TL			Can not found the library.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//------------------------------------------------------------------------
GX_API GXInitLib();

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief		�ر��豸�⣬�ͷ���Դ
\attention	�ͷſ���Դ�����û�����ʹ�ÿ��ʱ����ô˽ӿڡ�
			����û�֮ǰû�е���GXInitLib��ֱ�ӵ���GXCloseLib���ӿڷ��سɹ���
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			�������������μ�GX_STATUS_LIST

\English:
\brief		Close the device library to release resources.
\attention	Frees library resources and invokes this interface when the user is no longer using the library.
			If the user did not call GXInitLib before, call GXCloseLib directly and the interface returns success.
\return		GX_STATUS_SUCCESS				The operation is successful, no error occurs.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXCloseLib();

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ����ָ�����͵���־�ɷ���
\attention  �˽ӿڵ�����������ָ��������־�ɷ��ͣ�����֮ǰ��Ҫ����GXInitLib�ӿ�
\param      [in]ui32LogType                 ��־���ͣ� GX_LOG_TYPE_FATAL|GX_LOG_TYPE_ERROR|GX_LOG_TYPE_INFO
                                                      GX_LOG_TYPE_OFF�� ȫ�����ɷ���
\return     GX_STATUS_SUCCESS               �����ɹ���û�з�������
            GX_STATUS_NOT_INIT_API          û�е���GXInitLib��ʼ����
            GX_STATUS_INVALID_ACCESS        �����ļ�·������
            �������������μ�GX_STATUS_LIST

\English:
\brief      Set whether logs of the specified type can be sent
\attention  The function of this interface is to set whether the specified type of logs can be sent.
            The GXInitLib interface needs to be called before the invocation
\param      [in]ui32LogType                 log type:   GX_LOG_TYPE_FATAL|GX_LOG_TYPE_ERROR|GX_LOG_TYPE_INFO
                                                        GX_LOG_TYPE_OFF: all cannot be sent
\return		GX_STATUS_SUCCESS				The operation is successful and no error occurs.
            GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
            GX_STATUS_INVALID_ACCESS		The config file path is not available.
            The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXSetLogType(const uint32_t ui32LogType);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡָ�����͵���־�Ƿ�ɷ���
\attention  �˽ӿڵ������ǻ�ȡָ��������־�Ƿ�ɷ��ͣ�����֮ǰ��Ҫ����GXInitLib�ӿ�
\param      [out]pui32Value                  ��־�����Ƿ�ɷ��ͣ�FATAL|ERROR|WARN|INFO|DEBUG|TRACE
\return     GX_STATUS_SUCCESS               �����ɹ���û�з�������
            GX_STATUS_NOT_INIT_API          û�е���GXInitLib��ʼ����
            GX_STATUS_INVALID_PARAMETER     �û������ָ��ΪNULL
            GX_STATUS_INVALID_ACCESS        �����ļ�·������
            �������������μ�GX_STATUS_LIST

\English:
\brief      Gets whether logs of a specified type can be sent
\attention  The function of this interface is to get whether the specified type of logs can be sent.
            The GXInitLib interface needs to be called before the invocation
\param      [out]pui32Value                 log type value: FATAL|ERROR|WARN|INFO|DEBUG|TRACE
\return		GX_STATUS_SUCCESS				The operation is successful and no error occurs.
            GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
            GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
            GX_STATUS_INVALID_ACCESS		The config file path is not available.
            The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXGetLogType(uint32_t* pui32Value);

//------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡ�������Ĵ���������Ϣ
\attention  ���û����������ӿ�ʧ�ܵ�ʱ�򣬿��Ե��ô˽ӿڻ�ȡ����ʧ����Ϣ����ϸ����
\param		[out] pErrorCode		�������Ĵ����룬����û������ȡ��ֵ����ô�˲������Դ�NULL
\param		[out] pszErrText		���ش�����Ϣ��������ַ
\param		[in,out] pSize			������Ϣ��������ַ��С����λ�ֽ�
									���pszErrTextΪNULL��
									[out]pnSize����ʵ����Ҫ��buffer��С
									���pszErrText��NULL��
									[in]pnSizeΪʵ�ʷ����buffer��С
									[out]pnSize����ʵ�����buffer��С
\return		GX_STATUS_SUCCESS                �����ɹ���û�з�������
			GX_STATUS_INVALID_PARAMETER      �û������ָ��ΪNULL
			GX_STATUS_NEED_MORE_BUFFER       �û������buffer��С
			�������������μ�GX_STATUS_LIST

\English:
\brief      To get the latest error descriptions information of the program.
\attention  When a user fails to invoke another interface, the user can invoke this interface to obtain a detailed description of the failure information.
\param		[out] pErrorCode    Return the last error code. You could set the parameter to NULL if you don't need this value.
\param		[out] pszErrText    Return the address of the buffer allocated for error information.
\param		[in,out] pSize      The address size of the buffer allocated for error information. Unit: byte.
								If pszErrText is NULL:
								[out]pnSize Return the actual required buffer size.
								If pszErrText is not NULL:
								[in]pnSize It is the actual allocated buffer size.
								[out]pnSize Return the actual allocated buffer size.
\return		GX_STATUS_SUCCESS					The operation is successful and no error occurs.
			GX_STATUS_INVALID_PARAMETER			The pointer that the user input is NULL.
			GX_STATUS_NEED_MORE_BUFFER			The buffer that the user filled is too small.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//------------------------------------------------------------------------
GX_API GXGetLastError(GX_STATUS *pErrorCode, char *pszErrText, size_t *pSize);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief		ö���ض������豸���һ�ȡ�豸����
\attention	�˽ӿڵ������Ǹ��¿��ڲ��豸�б����˽ӿڻ�ı���ڲ��豸�б���
			���Ե���GXGetInterfaceInfo��GXGetInterfaceNum��GXGetInterfaceHandle��GXGetDeviceInfo��GXOpenDevice֮ǰ��Ҫ���ô˽ӿڡ�
			������û�ָ����ʱʱ���ڳɹ�ö�ٵ��豸�����������أ�������û�ָ����ʱʱ����û��ö�ٵ��豸����һֱ�ȴ���ֱ���ﵽ�û�ָ���ĳ�ʱʱ�䷵�ء�
\param		[in]nTLType				ö���ض����͵��豸���ο�GX_TL_TYPE_LIST
			[out]punNumDevices		�����豸����
\param		[in]unTimeOut			ö�ٵĳ�ʱʱ��(��λms)��
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER   �û������ָ��ΪNULL
			�������������μ�GX_STATUS_LIST

\English:
\brief		Enumerate a specific type of device and get the number of devices
\attention  The function of this interface is to update the list of internal devices in the library. This interface changes the list of internal devices in the library.
			Therefore, this interface must be invoked before calling GXGetInterfaceInfo, GXGetInterfaceNum, GXGetInterfaceHandle, GXGetDeviceInfo, and GXOpenDevice.
			If the device is successfully enumerated within the specified timeout time, the value returns immediately.
			If the device is not enumerated within the specified timeout time, then it waits until the specified timeout time is over and then it returns.
\param		[in]nTLType				Enumerate specific types of devices,reference to GX_TL_TYPE_LIST
			[out]punNumDevices		Return number of devices
\param		[in]unTimeOut			The timeout time of enumeration (unit: ms).
\return		GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXUpdateAllDeviceListEx( uint64_t nTLType, uint32_t* punNumDevices, uint32_t nTimeOut);

//----------------------------------------------------------------------------------
/**
\ Chinese��
\brief		ö�������豸���һ�ȡ�豸����,����ǧ�����豸�˽ӿ��ܹ�ö�����������ڵ��豸
\attention	�˽ӿڵ������Ǹ��¿��ڲ��豸�б����˽ӿڻ�ı���ڲ��豸�б���
			���Ե���GXGetInterfaceInfo��GXGetInterfaceNum��GXGetInterfaceHandle��GXGetDeviceInfo��GXOpenDevice֮ǰ��Ҫ���ô˽ӿڡ�
			������û�ָ����ʱʱ���ڳɹ�ö�ٵ��豸�����������أ�������û�ָ����ʱʱ����û��ö�ٵ��豸����һֱ�ȴ���ֱ���ﵽ�û�ָ���ĳ�ʱʱ�䷵��
\param		[out]punNumDevices			 �����豸����
\param		[in]unTimeOut				 ö�ٵĳ�ʱʱ��(��λms)��
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER   �û������ָ��ΪNULL
			�������������μ�GX_STATUS_LIST

\English:
\brief		Enumerate all devices and obtain the number of devices,
			For gige devices, this interface can enumerate devices in all subnets.
\attention	The function of this interface is to update the list of internal devices in the library. This interface changes the list of internal devices in the library.
			Therefore, this interface must be invoked before calling GXGetInterfaceInfo, GXGetInterfaceNum, GXGetInterfaceHandle, GXGetDeviceInfo, and GXOpenDevice.
			If the device is successfully enumerated within the specified timeout time, the value returns immediately.
			If the device is not enumerated within the specified timeout time, then it waits until the specified timeout time is over and then it returns.
\param		[out]punNumDevices			 Return number of devices
\param		[in]unTimeOut				 The timeout time of enumeration (unit: ms).
\return		GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXUpdateAllDeviceList      (uint32_t* punNumDevices, uint32_t nTimeOut);

//----------------------------------------------------------------------------------
/**
\ Chinese��
\brief		ͨ������ActionCommond����ʹ�����ϵ����ͬʱִ��action����
\attention	��ʹ��IPV4��ַ��groupMask���벻Ϊ0�����timeoutMs��Ϊ0��pNumResults��*pNumResults��pResults���벻Ϊ0
\param      [in]deviceKey			         Э��涨���豸��Կ
\param      [in]groupKey			         Э��涨������Կ
\param      [in]groupMask			         Э��涨��������
\param      [in]pBroadcastAddress			 ����cmd��Ŀ��ip,��Ϊ�㲥ip(255.255.255.255)�������㲥(192.168.42.255)������(192.168.42.42)
\param      [in]pSpecialAddress			     ��ѡ������cmd��Դip��������ȷ�ı�����ĸ������Ϸ�������ɽ��������������ظ�����cmd����
\                                            �����ָ�����㲥�������㲥����µ�ǰ������ÿһ�����ϵ��������ᷢaction
\param      [in]timeoutMs			         ��ѡ����Ӧack�ĳ�ʱʱ��(ms)��������յ� pNumResults����ֹͣ�ȴ�ȷ�ϡ�
\                                            �������Ҫ����������������˲�������Ϊ 0��
\param      [in][out]pNumResults			 ��ѡ����������еĽ������
\                                            ���ݵ�ֵӦ����ȷ�������Ԥ���豸������
\                                            ��� timeoutMs Ϊ 0������Դ˲�������ˣ���� timeoutMs Ϊ 0����˲�������Ϊ NULL��
\param      [in][out]pResults			     ��ѡ��һ������ *pNumResults Ԫ�ص����飬���ڱ������������״̬����������ͷ��ʼ��䡣
\                                            ����յ��Ľ�����ڿ��õĽ����򲻻����ʣ������
\                                            ��� timeoutMs Ϊ 0������Դ˲�������ˣ���� timeoutMs Ϊ 0����˲�������Ϊ NULL��
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			�������������μ�GX_STATUS_LIST

\English:
\brief		Sending the ActionCommon command allows cameras on the network to execute actions simultaneously.
\attention	 IP4 addresses must be used. groupMask must not be 0. pNumResult, *pNumResult and pResults must not be 0 if a timeoutMs value other than 0 is passed.
\param      [in]deviceKey			         Device key specified by the protocol
\param      [in]groupKey			         The group key specified by the protocol
\param      [in]groupMask			         Protocol-defined group mask
\param      [in]pBroadcastAddress			 The destination IP to send cmd to can be broadcast IP (255.255.255.255), subnet broadcast (192.168.42.255), unicast (192.168.42.42)
\param      [in]pSpecialAddress			     The source IP of the cmd is used to clearly identify the network port from which the command is sent. 
\                                            This can solve the problem of repeated cmd sending in the case of multiple network cards.
\param      [in]timeoutMs			         Optional: Time in milliseconds the call is waiting for acknowledges of the addressed devices. 
\                                            Waiting for acknowledges is stopped if pNumResults have been received. 
\                                            This parameter can be 0 if a check of action command results is not required.
\param      [in][out]pNumResults			 Optional: The number of results in the results array. 
\                                            The value passed should be equal to the expected number of devices that acknowledge the command. 
\                                            Returns the number of received results. This parameter is ignored if timeoutMs is 0. 
\                                            Thus, this parameter can be NULL if timeoutMs is 0.
\param      [in][out]pResults			     Optional: An array with *pNumResults elements to hold the action command result status. 
\                                            The buffer is filled beginning from the start. 
\                                            Remaining results are not changed if less results are received than result items available. 
\                                            This parameter is ignored if timeoutMs is 0. Thus, this parameter can be NULL if timeoutMs is 0.
\return		GX_STATUS_SUCCESS                The operation was successful and no errors occurred
            GX_STATUS_NOT_INIT_API            GXInitLib is not called to initialize the library
            For other error conditions, see GX_STATUS_LIST
*/
//----------------------------------------------------------------------------------
GX_API GXGigEIssueActionCommand( uint32_t nDeviceKey, uint32_t nGroupKey, 
                                uint32_t nGroupMask, const char* strBroadcastAddress, 
                                const char* strSpecialAddress, uint32_t nTimeoutMs, 
                                uint32_t* pnNumResults, GX_GIGE_ACTION_COMMAND_RESULT* pstResults );

//----------------------------------------------------------------------------------
/**
\ Chinese��
\brief		ͨ������ActionCommond����ʹ�����ϵ����ͬʱִ��action����
\attention	��ʹ��IPV4��ַ��groupMask���벻Ϊ0�����timeoutMs��Ϊ0��pNumResults��*pNumResults��pResults���벻Ϊ0
\param      [in]deviceKey			         Э��涨���豸��Կ
\param      [in]groupKey			         Э��涨������Կ
\param      [in]groupMask			         Э��涨��������
\param      [in]pBroadcastAddress			 ����cmd��Ŀ��ip,��Ϊ�㲥ip(255.255.255.255)�������㲥(192.168.42.255)������(192.168.42.42)
\param      [in]actiontimeNs			     ִ�в�����ʱ�䣨������Ϊ��λ����ʵ��ֵȡ������ʹ�õ���ʱ�ӡ�
\                                            ���磬����ͨ���ڴ�һ������豸������ʱ���ֵGXSetCommandValue(hDevice, "TimestampLatch")
\                                            ���ȡʱ���ֵGXGetIntValue(hDevice, "TimestampLatchValue", &pstIntValue) ����ȡһ��ͬ������豸����ʱ��ֵ��
\param      [in]pSpecialAddress			     ��ѡ������cmd��Դip��������ȷ�ı�����ĸ������Ϸ�������ɽ��������������ظ�����cmd����,
\                                            �����ָ�����㲥�������㲥����µ�ǰ������ÿһ�����ϵ��������ᷢaction
\param      [in]timeoutMs			         ��ѡ����Ӧack�ĳ�ʱʱ��(ms)��������յ� pNumResults����ֹͣ�ȴ�ȷ�ϡ�
\                                            �������Ҫ����������������˲�������Ϊ 0��
\param      [in][out]pNumResults			 ��ѡ����������еĽ������
\                                            ���ݵ�ֵӦ����ȷ�������Ԥ���豸������
\                                            ��� timeoutMs Ϊ 0������Դ˲�������ˣ���� timeoutMs Ϊ 0����˲�������Ϊ NULL��
\param      [in][out]pResults			     ��ѡ��һ������ *pNumResults Ԫ�ص����飬���ڱ������������״̬����������ͷ��ʼ��䡣
\                                            ����յ��Ľ�����ڿ��õĽ����򲻻����ʣ������
\                                            ��� timeoutMs Ϊ 0������Դ˲�������ˣ���� timeoutMs Ϊ 0����˲�������Ϊ NULL��
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			�������������μ�GX_STATUS_LIST

\English:
\brief		Sending the ActionCommon command allows cameras on the network to execute actions simultaneously.
\attention	IP4 addresses must be used. groupMask must not be 0. pNumResult, *pNumResult and pResults must not be 0 if a timeoutMs value other than 0 is passed.
\param      [in]deviceKey			         Device key specified by the protocol
\param      [in]groupKey			         The group key specified by the protocol
\param      [in]groupMask			         Protocol-defined group mask
\param      [in]actiontimeNs                 The time in nanoseconds to perform the operation.The actual value depends on the master clock used.
\                                            For example, the master clock value of a set of synchronized camera devices can be obtained by reading the 
\                                            timestamp value GXGetIntValue(hDevice, "TimestampLatch") after latching it from the set of camera devices
\param      [in]pBroadcastAddress			 The destination IP to send cmd to can be broadcast IP (255.255.255.255), subnet broadcast (192.168.42.255), unicast (192.168.42.42)
\param      [in]pSpecialAddress			     Optional: The source IP of the cmd is used to clearly identify the network port from which the command is sent. 
\                                            This can solve the problem of repeated cmd sending in the case of multiple network cards.
\param      [in]timeoutMs			         Optional: Time in milliseconds the call is waiting for acknowledges of the addressed devices. 
\                                            Waiting for acknowledges is stopped if pNumResults have been received. 
\                                            This parameter can be 0 if a check of action command results is not required.
\param      [in][out]pNumResults			 Optional: The number of results in the results array. 
\                                            The value passed should be equal to the expected number of devices that acknowledge the command. 
\                                            Returns the number of received results. This parameter is ignored if timeoutMs is 0. 
                                             Thus, this parameter can be NULL if timeoutMs is 0.
\param      [in][out]pResults			     Optional: An array with *pNumResults elements to hold the action command result status. 
\                                            The buffer is filled beginning from the start. 
\                                            Remaining results are not changed if less results are received than result items available. 
\                                            This parameter is ignored if timeoutMs is 0. Thus, this parameter can be NULL if timeoutMs is 0.
\return		GX_STATUS_SUCCESS                The operation was successful and no errors occurred
            GX_STATUS_NOT_INIT_API            GXInitLib is not called to initialize the library
            For other error conditions, see GX_STATUS_LIST
*/
//----------------------------------------------------------------------------------
GX_API GXGigEIssueScheduledActionCommand( uint32_t nDeviceKey, uint32_t nGroupKey, 
                                         uint32_t nGroupMask, uint64_t nActiontimeNs, 
                                         const char* strBroadcastAddress, const char* strSpecialAddress, 
                                         uint32_t nTimeoutMs, uint32_t* pnNumResults, 
                                         GX_GIGE_ACTION_COMMAND_RESULT* pstResults );

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡö�ٵ���Interface�б�����
\attention  �˽ӿڵ������ǻ�ȡö�ٵ���Interface������
			����֮ǰ��Ҫ����GXUpdateAllDeviceList��GXUpdateAllDeviceListEx�ӿڡ�
\param      [in,out]punNumInterfaces      ����Interface����
\return     GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER   �û������ָ��ΪNULL
			�������������μ�GX_STATUS_LIST

\English:
\brief      Obtain the number of interface lists enumerated.
\attention  The purpose of this Interface is to get the number of interfaces enumerated,
			please call GXUpdateAllDeviceList, GXUpdateAllDeviceListEx interfaces before calling.
\param      [in,out]punNumInterfaces		Return the number of interfaces
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXGetInterfaceNum(uint32_t* punNumInterfaces);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡ����ΪIndex��Interface��Ϣ
\attention  �˽ӿڵ������ǻ�ȡö�ٵ�Interface�ӿڣ�
            ����֮ǰ��Ҫ����GXUpdateAllDeviceList��GXUpdateAllDeviceListEx�ӿ�
\param      [in]nIndex					Interface ���򣬴ӿ�ʼ 1
            [in,out]pstInterfaceInfo	����Interface��Ϣ
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
            GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
            GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
            �������������μ�GX_STATUS_LIST

\English:
\brief      Obtain basic interface information nIndex device.
\attention  The purpose of this interface is to obtain the enumerated Interface Info interface,
			please call GXUpdateAllDeviceList, GXUpdateAllDeviceListEx interface before calling.
\param      [in]nIndex					Interface order, starting from 1
			[out]pstInterfaceInfo       Return interface information
\return		GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXGetInterfaceInfo(uint32_t nIndex, GX_INTERFACE_INFO* pstInterfaceInfo);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡ����ΪIndex��Interface Handle
\attention  �˽ӿڵ������ǻ�ȡö�ٵ�Interface Handle�Ľӿڣ�
            ����֮ǰ��Ҫ����GXUpdateAllDeviceList��GXUpdateAllDeviceListEx�ӿ�
\param      [in]nIndex              Interface���򣬴�1��ʼ
            [in,out]phIF			����Interface���
\return     GX_STATUS_SUCCESS             �����ɹ���û�з�������
            GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
            GX_STATUS_INVALID_PARAMETER   �û������ָ��ΪNULL
            �������������μ�GX_STATUS_LIST

\English:
\brief      Get Interface Handles of Index.
\attention  The purpose of this interface is to obtain the enumerated Handle interface,
			please call GXUpdateAllDeviceList, GXUpdateAllDeviceListEx interface before calling.
\param      [in]nIndex					Interface order, starting from 1
			[in,out]phIF				Return Interface handle
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXGetInterfaceHandle(uint32_t nIndex, GX_IF_HANDLE* phIF);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡ����ΪIndex���豸��Ϣ
\attention  �˽ӿڵ������ǻ�ȡö�ٵ��豸��Ϣ�ӿڣ�
            ����֮ǰ��Ҫ����GXUpdateAllDeviceList��GXUpdateAllDeviceListEx�ӿ�
\param      [in]nIndex                    �豸���򣬴�1��ʼ
            [in,out]pstDeviceInfo         �����豸��Ϣ
\return     GX_STATUS_SUCCESS             �����ɹ���û�з�������
            GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
            GX_STATUS_INVALID_PARAMETER   �û������ָ��ΪNULL
            �������������μ�GX_STATUS_LIST

\English:
\brief      Obtain basic information of nIndex devices
\attention  The function of this interface is to obtain enumeration to the device information interface,
			please call GXUpdateAllDeviceList, GXUpdateAllDeviceListEx interface before calling.
\param      [in]nIndex                    Device order,starting from 1
			[in,out]pstDeviceInfo         Return Device information
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXGetDeviceInfo(uint32_t nIndex, GX_DEVICE_INFO* pstDeviceInfo);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief		ͨ��ָ��Ψһ��ʾ���豸������ָ��SN��IP��MAC��
\attention	�˽ӿڵ���֮ǰ��Ҫ����GXUpdateAllDeviceList��GXUpdateAllDeviceListEx�ӿڣ����¿��ڲ��豸�б�
\param		[in]pOpenParam		�û����õĴ��豸����,�μ�GX_OPEN_PARAM�ṹ�嶨��
\param		[out]phDevice		�����豸���
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER   �û������ָ��ΪNULL
			GX_STATUS_NOT_FOUND_DEVICE    û���ҵ���ָ����Ϣƥ����豸
			GX_STATUS_INVALID_ACCESS      �豸�ķ��ʷ�ʽ����
			�������������μ�GX_STATUS_LIST

\English:
\brief		Open the device by a specific unique identification, such as: SN, IP, MAC, Index etc.
\attention	Before invoking this interface, you need to call the GXUpdateAllDeviceList or GXUpdateAllDeviceListEx interfaces to update the device list in the library
\param		[in]pOpenParam		The open device parameter which is configurated by user,Ref:GX_OPEN_PARAM.
\param		[out]phDevice		Return device handle
\return		GX_STATUS_SUCCESS             The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API        The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER   The pointer that the user input is NULL.
			GX_STATUS_NOT_FOUND_DEVICE    Not found the device that matches the specific information.
			GX_STATUS_INVALID_ACCESS      The device can not be opened under the current access mode.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXOpenDevice               (GX_OPEN_PARAM* pOpenParam, GX_DEV_HANDLE* phDevice);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief		ָ���豸����ر��豸
\attention	�����ظ��ر�ͬһ���豸
\param		[in]hDevice			����Ҫ�رյ��豸���
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief		Specify the device handle to close the device.
\attention	The same device cannot be shut down repeatedly.
\param		[in]hDevice			The device handle that the user specified to close.
\return		GX_STATUS_SUCCESS             The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API        The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_HANDLE      The illegal handle that the user introduces, or reclose thedevice.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXCloseDevice              (GX_DEV_HANDLE hDevice);


// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡ�豸������Interface���
\param		[in]hDevice			    �豸handle
\param		[in,out]phIF            Interface Handle
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
            GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
            GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
            �������������μ�GX_STATUS_LIST

\English:
\brief      Obtain the device's interface handle.
\param		[in]hDevice			    Device handle
\param		[in,out]phIF            Interface Handle
\return		GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose thedevice.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXGetParentInterfaceFromDev(GX_DEV_HANDLE hDevice, GX_IF_HANDLE* phIF);


// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡ�����豸handle
\param		[in]hDevice			    �豸handle
\param		[in,out]phLocalDev      �����豸Handle
\return     GX_STATUS_SUCCESS             �����ɹ���û�з�������
            GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
            GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
            �������������μ�GX_STATUS_LIST

\English:
\brief      Get local device handle.
\param		[in]hDevice			    Device handle
\param		[in,out]phLocalDev      Local device handle
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXGetLocalDeviceHandleFromDev(GX_DEV_HANDLE hDevice, GX_LOCAL_DEV_HANDLE* phLocalDev);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡ�豸��ͨ������
\param		[in]hDevice			    �豸handle
\param		[in|out]pnDSNum         �豸��ͨ������
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
            GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
            GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
            �������������μ�GX_STATUS_LIST

\English:
\brief      Obtain the number of data stream.
\param		[in]hDevice			    Device handle
\param		[in|out]pnDSNum         Data stream number
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXGetDataStreamNumFromDev(GX_DEV_HANDLE hDevice, uint32_t* pnDSNum);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡ�豸��ͨ�����
\param		[in]hDevice			    �豸handle
\param		[in]nDSIndex			�豸����ţ���Ŵ�1 ��ʼ
\param		[in|out]phDS            �豸��handle
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
            GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
            GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
            �������������μ�GX_STATUS_LIST

\English:
\brief      Get data stream handle.
\param		[in]hDevice			    Device handle
\param		[in]nDSIndex			Data stream order, starting from 1
\param		[in|out]phDS            Data stream handle
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXGetDataStreamHandleFromDev(GX_DEV_HANDLE hDevice, uint32_t nDSIndex, GX_DS_HANDLE* phDS);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief     ��ȡ�豸������IP��Ϣ
\attention �ýӿ�ֻ�����������豸
\param     [in]       hDevice                  �豸���
\param     [in]       pszIP                    �豸����IP�ַ�����ַ
\param     [in, out]  pnIPLength               �豸����IP��ַ�ַ�������,��λ�ֽڡ�
\param     [in]       pnIPLength:              �û�buffer��С
\param     [out]      pnIPLength:              ʵ������С
\param     [in]       pszSubNetMask            �豸�������������ַ�����ַ
\param     [in, out]  pnSubNetMaskLength       �豸�������������ַ�������
\param     [in]       pnSubNetMaskLength:      �û�buffer��С
\param     [out]      pnSubNetMaskLength:      ʵ������С
\param     [in]       pszDefaultGateWay        �豸���������ַ�����ַ
\param     [in, out]  pnDefaultGateWayLength   �豸���������ַ�������
\param     [in]       pnDefaultGateWayLength:  �û�buffer��С
\param     [out]      pnDefaultGateWayLength:  ʵ������С
\return    GX_STATUS_SUCCESS             �����ɹ���û�з�������
		   GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
		   GX_STATUS_INVALID_PARAMETER   �û������ָ��ΪNULL
		   ����û�к��ǵ��ģ��������Ĵ��������μ�GX_STATUS_LIST

\English:
\brief     Obtain the permanent IP information of the device.
\attention This interface applies only to network devices
\param     [in]       hDevice                  The handle of the device.
\param     [in]       pszIP                    Device permanent IP string address
\param     [in, out]  pnIPLength               The length of the device's permanent IP address string (in bytes).
\param     [in]       pnIPLength:              User buffer size
\param     [out]      pnIPLength:              Actual fill size
\param     [in]       pszSubNetMask            Device persistent subnet mask string address
\param     [in, out]  pnSubNetMaskLength       Device persistent subnet mask string length
\param     [in]       pnSubNetMaskLength:      User buffer size
\param     [out]      pnSubNetMaskLength:      Actual fill size
\param     [in]       pszDefaultGateWay        Device permanent gateway string address
\param     [in, out]  pnDefaultGateWayLength   Device permanent gateway string length
\param     [in]       pnDefaultGateWayLength:  User buffer size
\param     [out]      pnDefaultGateWayLength:  Actual fill size
\return    GX_STATUS_SUCCESS             The operation is successful and no error occurs.
		   GX_STATUS_NOT_INIT_API        The GXInitLib initialization library is not called.
		   GX_STATUS_INVALID_PARAMETER   The input parameter that the user introduces is invalid.
		   The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXGetDevicePersistentIpAddress (GX_DEV_HANDLE hDevice,
                                       char* pszIP,
                                       size_t *pnIPLength,
                                       char* pszSubNetMask,
                                       size_t *pnSubNetMaskLength,
                                       char* pszDefaultGateWay,
                                       size_t *pnDefaultGateWayLength);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief     �����豸������IP��Ϣ
\attention �ýӿ�ֻ�����������豸
\param     [in]     hDevice              �豸���
\param     [in]     pszIP                �豸����IP�ַ�����ĩβ��\0��
\param     [in]     pszSubNetMask        �豸�������������ַ�����ĩβ��\0��
\param     [in]     pszDefaultGateWay    �豸���������ַ�����ĩβ��\0��
\return    GX_STATUS_SUCCESS             �����ɹ���û�з�������
           GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
		   GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
		   �������������μ�GX_STATUS_LIST

\English:
\brief     Set the permanent IP information of the device.
\attention This interface applies only to network devices
\param     [in]     hDevice              The handle of the device.
\param     [in]     pszIP                Device permanent IP string address, end with ��\0��.
\param     [in]     pszSubNetMask        Device persistent subnet mask string address, end with ��\0��.
\param     [in]     pszDefaultGateWay    Device permanent gateway string address, end with ��\0��.
\return    GX_STATUS_SUCCESS             The operation is successful and no error occurs.
		   GX_STATUS_NOT_INIT_API        The GXInitLib initialization library is not called.
		   GX_STATUS_INVALID_HANDLE      The illegal handle that the user introduces, or reclose the device.
		   The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXSetDevicePersistentIpAddress (GX_DEV_HANDLE  hDevice,
									   const char* pszIP,
									   const char* pszSubNetMask,
									   const char* pszDefaultGateWay);


// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ����Զ���豸��̬�����ã�IP��ַ
\param		[in]   pszDevcieMacAddress  �豸MAC��ַ
\param		[in]   ui32IpConfigFlag     ip���÷�ʽ����̬IP��DHCP��LLA��Ĭ�Ϸ�ʽ��
\param		[in]   pszIPAddress         �豸IP��ַ
\param		[in]   pszSubnetMask        ��������
\param		[in]   pszDefaultGateway    ����
\param		[in]   pszUserID            �û��Զ�������
\return     GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER   ��Ч����
			GX_STATUS_NOT_FOUND_DEVICE    û���ҵ��豸
			GX_STATUS_ERROR               ����ʧ��
			GX_STATUS_INVALID_ACCESS      �ܾ�����
			GX_STATUS_TIMEOUT             ������ʱ
			GC_ERR_IO                     IOͨѶ����
			GC_ERR_INVALID_ID             ID�޷�����Դ��������

\English:
\brief      Configure the static IP address of the camera.
\param      [in]pszDeviceMacAddress The MAC address of the device.
\param      [in]ui32IpConfigFlag    IP Configuration.
\param      [in]pszIPAddress        The IP address to be set.
\param      [in]pszSubnetMask       The subnet mask to be set.
\param      [in]pszDefaultGateway   The default gateway to be set.
\param      [in]pszUserID           The user-defined name to be set.
\retrun     GX_STATUS_SUCCESS               The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API          The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER     The parameter is invalid.
			GX_STATUS_NOT_FOUND_DEVICE      Can not found the device.
			GX_STATUS_ERROR                 The operation is failed.
			GX_STATUS_INVALID_ACCESS        Access denied.
			GX_STATUS_TIMEOUT               The operation is timed out.
			GC_ERR_IO                       IO error.
			GC_ERR_INVALID_ID               Invalid ID.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXGigEIpConfiguration(const char* pszDeviceMacAddress,
							 GX_IP_CONFIGURE_MODE emIpConfigMode,
							 const char* pszIpAddress,
							 const char* pszSubnetMask,
							 const char* pszDefaultGateway,
							 const char* pszUserID);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ForceIP
\param		[in]   pszDevcieMacAddress  �豸MAC��ַ
\param		[in]   pszIPAddress         �豸IP��ַ
\param		[in]   pszSubnetMask        ��������
\param		[in]   pszDefaultGateway    ����
\return		GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		��Ч����
			GX_STATUS_NOT_FOUND_DEVICE		û���ҵ��豸
			GX_STATUS_ERROR					����ʧ��
			GX_STATUS_INVALID_ACCESS		�ܾ�����
			GX_STATUS_TIMEOUT				������ʱ
			GC_ERR_IO						IOͨѶ����
			GC_ERR_INVALID_ID				ID�޷�����Դ��������

\English:
\brief      Execute the Force IP.
\param      [in]pszDeviceMacAddress The MAC address of the device.
\param      [in]pszIPAddress        The IP address to be set.
\param      [in]pszSubnetMask       The subnet mask to be set.
\param      [in]pszDefaultGateway   The default gateway to be set.
\retrun     GX_STATUS_SUCCESS               The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API          The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER     The parameter is invalid.
			GX_STATUS_NOT_FOUND_DEVICE      Can not found the device.
			GX_STATUS_ERROR                 The operation is failed.
			GX_STATUS_INVALID_ACCESS        Access denied.
			GX_STATUS_TIMEOUT               The operation is timed out.
			GC_ERR_IO                       IO error.
			GC_ERR_INVALID_ID               Invalid ID.
*/
// ---------------------------------------------------------------------------
GX_API GXGigEForceIp(const char* pszDeviceMacAddress,
							 const char* pszIpAddress,
							 const char* pszSubnetMask,
							 const char* pszDefaultGateway);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ����/��λ
\param		[in]   pszDevcieMacAddress  �豸MAC��ַ
\param		[in]   ui32FeatureInfo      �����豸ģʽ
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		��Ч����
			GX_STATUS_NOT_FOUND_DEVICE		û���ҵ��豸
			GX_STATUS_ERROR					����ʧ��
			GX_STATUS_INVALID_ACCESS		�ܾ�����
			GX_STATUS_TIMEOUT				������ʱ
			GC_ERR_IO						IOͨѶ����
			GC_ERR_INVALID_ID				ID�޷�����Դ��������

\English:
\brief      Reconnection/Reset
\param      [in]pszDeviceMacAddress The MAC address of the device.
\param      [in]ui32FeatureInfo     Reconnection mode.
\retrun     GX_STATUS_SUCCESS               The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API          The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER     The parameter is invalid.
			GX_STATUS_NOT_FOUND_DEVICE      Can not found the device.
			GX_STATUS_ERROR                 The operation is failed.
			GX_STATUS_INVALID_ACCESS        Access denied.
			GX_STATUS_TIMEOUT               The operation is timed out.
			GC_ERR_IO                       IO error.
			GC_ERR_INVALID_ID               Invalid ID.
*/
// ---------------------------------------------------------------------------
GX_API GXGigEResetDevice(const char* pszDeviceMacAddress, GX_RESET_DEVICE_MODE ui32FeatureInfo);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡ�豸��PayloadSize
\param		[in]hDStream			   �豸��handle
\param		[in|out]punPacketSize      PayLoadSizeָ��
\return     GX_STATUS_SUCCESS             �����ɹ���û�з�������
            GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
            GX_STATUS_INVALID_PARAMETER   �û������ָ��ΪNULL
            �������������μ�GX_STATUS_LIST

\English:
\brief      Get device stream PayloadSize.
\param		[in]hDStream			   Device stream handle.
\param		[in|out]punPacketSize      PayLoadSize pointer
\return     GX_STATUS_SUCCESS             The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API        The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER   The pointer that the user input is NULL.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXGetPayLoadSize(GX_DS_HANDLE hDStream, uint32_t* punPacketSize);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      �����û�������
\param		[in]hPort               Handle���
\param		[in]strFileName         �����û���������ļ�·��
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      Save the user parameter group
\param		[in]hPort               Handle
\param		[in]strFileName         File path to save user parameter groups
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXFeatureSave(GX_PORT_HANDLE hPort, const char* strFileName);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      �����û�������
\param		[in]hPort               Handle���
\param		[in]strFileName         �����û���������ļ�·��
\param		[in]bVerify             �����ֵΪtrue�����е����ȥ��ֵ���ᱻ��������У���Ƿ�һ��
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      Load the user parameter group
\param		[in]hPort               Handle
\param		[in]strFileName         File path for loading user parameter groups
\param		[in]bVerify             If the value is true, All imported values will be read out for consistency verification
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXFeatureLoad(GX_PORT_HANDLE hPort, const char* strFileName, bool bVerify);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      �����û�������
\param		[in]hPort               Handle���
\param		[in]strFileName         �����û���������ļ�·�������ֽڣ�
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      Save the user parameter group
\param		[in]hPort               Handle
\param		[in]strFileName         File path to save user parameter groups(Wide byte)
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXFeatureSaveW(GX_PORT_HANDLE hPort, const wchar_t* strFileName);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      �����û�������
\param		[in]hPort               Handle���
\param		[in]strFileName         �����û���������ļ�·�������ֽڣ�
\param		[in]bVerify             �����ֵΪtrue�����е����ȥ��ֵ���ᱻ��������У���Ƿ�һ��
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      Load the user parameter group
\param		[in]hPort               Handle
\param		[in]strFileName         File path for loading user parameter groups(Wide byte)
\param		[in]bVerify             If the value is true, All imported values will be read out for consistency verification
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXFeatureLoadW(GX_PORT_HANDLE hPort, const wchar_t* strFileName, bool bVerify);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡ�ڵ�Ķ�д����
\param		[in]hPort					Handle���
\param		[in]strName					�ڵ�����
\param		[in|out]pemAccessMode      �ڵ��д����
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      Get the read and write attributes of the node.
\param		[in]hPort					The handle.
\param		[in]strName					Node name.
\param		[in|out]pemAccessMode		Read and write attributes of the node.
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXGetNodeAccessMode(GX_PORT_HANDLE hPort, const char* strName, GX_NODE_ACCESS_MODE* pemAccessMode);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡ���ͽڵ���Ϣ
\param		[in]       hPort              Handle���
\param		[in]       strName            �ڵ�����
\param		[in|out]   pstIntValue        ���ͽڵ���Ϣ
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      Get int value information.
\param		[in]hPort				The handle.
\param		[in]strName				Node name.
\param		[in|out]pstIntValue     Point to the pointer of the current returned value.
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXGetIntValue(GX_PORT_HANDLE hPort, const char* strName, GX_INT_VALUE* pstIntValue);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      �������ͽڵ�ֵ
\param		[in]hPort              Handle���
\param		[in]strName            �ڵ�����
\param		[in|out]i64Value       ���ͽڵ�ֵ
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      Set int value information.
\param		[in]hPort				The handle.
\param		[in]strName				Node name.
\param		[in|out]i64Value        Integer node value
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXSetIntValue(GX_PORT_HANDLE hPort, const char* strName, int64_t i64Value);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡö���ͽڵ���Ϣ
\param		[in]hPort				Handle���
\param		[in]strName				�ڵ�����
\param		[in|out]pstEnumValue    ö�����ͽڵ���Ϣ����ֵ��ֵ��
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      To get the current enumeration value
\param		[in]hPort				The handle.
\param		[in]strName				Node name.
\param		[in|out]pstEnumValue    The value of the enumeration type.(numeric value)
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXGetEnumValue(GX_PORT_HANDLE hPort, const char* strName, GX_ENUM_VALUE* pstEnumValue);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ����ö���ͽڵ�ֵ
\param		[in]hPort           Handle���
\param		[in]strName         �ڵ�����
\param		[in]i64Value        ö�����ͽڵ�ֵ
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      Sets the value of an enumerated node
\param		[in]hPort           The handle.
\param		[in]strName         Node name.
\param		[in]i64Value        Enumerate type node values
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXSetEnumValue(GX_PORT_HANDLE hPort, const char* strName, int64_t i64Value);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ����ö���ͽڵ�ֵ
\param		[in]hPort           Handle���
\param		[in]strName         �ڵ�����
\param		[in]strValue        ö�����ͽڵ�ֵ���ַ�����ֵ��
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      Sets the value of an enumerated node
\param		[in]hPort           The handle.
\param		[in]strName         Node name.
\param		[in]strValue        Enumerate type node values.(String value)
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXSetEnumValueByString(GX_PORT_HANDLE hPort, const char* strName, const char* strValue);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡ�����ͽڵ���Ϣ
\param		[in]hPort				Handle���
\param		[in]strName				�ڵ�����
\param		[in|out]pstFloatValue   �������ͽڵ���Ϣ
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      Get the value of float type.
\param		[in]hPort				The handle.
\param		[in]strName				Node name.
\param		[in|out]pstFloatValue   Float node information.
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXGetFloatValue(GX_PORT_HANDLE hPort, const char* strName, GX_FLOAT_VALUE* pstFloatValue);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ���ø����ͽڵ�ֵ
\param		[in]hPort           Handle���
\param		[in]strName         �ڵ�����
\param		[in]dValue          �������ͽڵ�ֵ
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      Set the value of float type.
\param		[in]hPort           The handle.
\param		[in]strName         Node name.
\param		[in]dValue          Floating point node value
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXSetFloatValue(GX_PORT_HANDLE hPort, const char* strName, double dValue);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡ�����ͽڵ���Ϣ
\param		[in]hPort              Handle���
\param		[in]strName            �ڵ�����
\param		[in|out]pbValue        �������ͽڵ���Ϣ
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      Get the value of bool type.
\param		[in]hPort              The handle.
\param		[in]strName            Node name.
\param		[in|out]pbValue        Boolean node information
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXGetBoolValue(GX_PORT_HANDLE hPort, const char* strName, bool* pbValue);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ���ò����ͽڵ�ֵ
\param		[in]hPort           Handle���
\param		[in]strName         �ڵ�����
\param		[in]bValue          �������ͽڵ�ֵ
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST


\English:
\brief      Set the value of bool type.
\param		[in]hPort           The handle.
\param		[in]strName         Node name.
\param		[in]bValue          Boolean node value
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXSetBoolValue(GX_PORT_HANDLE hPort, const char* strName, bool bValue);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡ�ַ����ͽڵ���Ϣ
\param		[in]hPort					Handle���
\param		[in]strName					�ڵ�����
\param		[in|out]pstStringValue		�ַ������ͽڵ�����
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      Get the content of the string type.
\param		[in]hPort					The handle.
\param		[in]strName					Node name.
\param		[in|out]pstStringValue		String type node data.
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXGetStringValue(GX_PORT_HANDLE hPort, const char* strName, GX_STRING_VALUE* pstStringValue);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      �����ַ������ͽڵ�ֵ
\param		[in]hPort				Handle���
\param		[in]strName				�ڵ�����
\param		[in]strValue			�ַ������ͽڵ�ֵ
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      Set a string type node value
\param		[in]hPort				The handle.
\param		[in]strName				Node name.
\param		[in]strValue			String type Node value
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXSetStringValue(GX_PORT_HANDLE hPort, const char* strName, const char* strValue);

//string ����

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ��������
\param		[in]hPort				Handle���
\param		[in]strName				�ڵ�����
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      Send the command.
\param		[in]hPort				The handle.
\param		[in]strName				Node name.
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXSetCommandValue(GX_PORT_HANDLE hPort, const char* strName);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡ�Ĵ����ͽڵ����ݳ���
\param		[in]hPort              Handle���
\param		[in]strName            �ڵ�����
\param		[in|out]pnSize         �������ݳ���
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      Gets the data length of a register node
\param		[in]hPort              The handle.
\param		[in]strName            Node name.
\param		[in|out]pnSize         Save the data length.
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXGetRegisterLength(GX_PORT_HANDLE hPort, const char* strName, size_t* pnSize);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ȡ�Ĵ����ͽڵ�ֵ
\param		[in]hPort              Handle���
\param		[in]strName            �ڵ�����
\param		[in|out]pBuffer        �Ĵ���ֵ����
\param		[in|out]pnSize         ���ݳ���
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      Get register type node values.
\param		[in]hPort              The handle.
\param		[in]strName            Node name.
\param		[in|out]pBuffer        Register value data
\param		[in|out]pnSize         Data length
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXGetRegisterValue(GX_PORT_HANDLE hPort, const char* strName, uint8_t* pBuffer, size_t* pnSize);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ���üĴ���ֵ
\param		[in]hPort           Handle���
\param		[in]strName         �ڵ�����
\param		[in]pBuffer         �Ĵ�������
\param		[in]nSize           �Ĵ������ݳ���
\return     GX_STATUS_SUCCESS				�����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API			û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_PARAMETER		�û������ָ��ΪNULL
			GX_STATUS_INVALID_HANDLE		�û�����Ƿ��ľ�������߹ر��Ѿ����رյ��豸
			�������������μ�GX_STATUS_LIST

\English:
\brief      Set register value
\param		[in]hPort           The handle.
\param		[in]strName         Node name.
\param		[in]pBuffer         Register data
\param		[in]nSize           Register data length
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER		The pointer that the user input is NULL.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXSetRegisterValue(GX_PORT_HANDLE hPort, const char* strName, uint8_t* pBuffer, size_t nSize);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ���û�ָ���Ĵ�����ֵ
\param		[in]hPort           ���
\param		[in]ui64Address     �Ĵ�����ַ
\param		[out]pBuffer        ���ؼĴ�����ֵ������ΪNULL
\param		[in, out]piSize     [in]�û������Buffer��С
								[out]�ɹ���ȡ�Ĵ�����ֵ�󣬷���ʵ�ʴ�С
\return     GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			����û�к��ǵ��ģ��������Ĵ��������μ�GX_STATUS_LIST

\English:
\brief      Reads the value of a user-specified register
\param		[in]hPort           The handle
\param		[in]ui64Address     Register address
\param		[out]pBuffer        Returns the value of the register, which cannot be NULL
\param		[in, out]piSize     [in]Buffer size applied by the user
								[out]After successfully reading the value of the register, the actual size is returned
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXReadPort(GX_PORT_HANDLE hPort, uint64_t ui64Address, void* pBuffer, size_t* piSize);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ���û�ָ���ļĴ�����д���û�����������
\param		[in]hPort           ���
\param		[in]ui64Address     �Ĵ�����ַ
\param		[in]pBuffer         �Ĵ�����ֵ������ΪNULL
\param		[in, out]piSize     [in]�û�Ҫд���Buffer����
								[out]����ʵ��д��Ĵ����ĳ���
\return     GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			����û�к��ǵ��ģ��������Ĵ��������μ�GX_STATUS_LIST

\English:
\brief      Writes the data given by the user to a register specified by the user
\param		[in]hPort           The handle
\param		[in]ui64Address     Register address
\param		[in]pBuffer         Register value, which cannot be NULL
\param		[in, out]piSize     [in]The Buffer length to be written by the user
								[out]Returns the length of the actual register written
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXWritePort(GX_PORT_HANDLE hPort, uint64_t ui64Address, const void* pBuffer, size_t* piSize);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      �������û�ָ���Ĵ�����ֵ����������ֵΪ4�ֽڳ��ȵļĴ���,�������Ͳ���֤������Ч�ԣ�
\param		[in]hPort           ���
\param		[in|out]pstEntries  [in]������ȡ�Ĵ����ĵ�ַ��ֵ
								[out]��ȡ����Ӧ�Ĵ���������
\param		[in, out]piSize     [in]��ȡ�豸�Ĵ����ĸ���
								[out]�ɹ���ȡ�Ĵ����ĸ���
\return     GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			����û�к��ǵ��ģ��������Ĵ��������μ�GX_STATUS_LIST

\English:
\brief      Batch read the value of the user specified register (only the command value is 4 bytes long register, other types do not guarantee data validity)
\param		[in]hPort           The handle
\param		[in|out]pstEntries  [in]Read register addresses and values in batches
								[out]The data is read from the corresponding register
\param		[in, out]piSize     [in]Read the number of device registers
								[out]The number of registers read successfully
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXReadPortStacked(GX_PORT_HANDLE hPort, GX_REGISTER_STACK_ENTRY* pstEntries, size_t *piSize);


//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      �������û�ָ���ļĴ�����д���û����������ݣ���������ֵΪ4�ֽڳ��ȵļĴ�����
            ���õ�ǰ�ӿں�ʹ��GXGetEnum��GXGetInt��GXGetBool�Ƚӿڻ�ȡ���Ľڵ�ֵΪ�޸�ǰֵ
            ��ʹ��GXReadPort�ӿڻ�ȡ���µļĴ���ֵ
\param		[in]hPort           ���
\param		[in]pstEntries      [in]����д�Ĵ����ĵ�ַ��ֵ
\param		[in|out]piSize      [in]�����豸�Ĵ����ĸ���
								[out]�ɹ�д�Ĵ����ĸ���
\return     GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			����û�к��ǵ��ģ��������Ĵ��������μ�GX_STATUS_LIST

\English:
\brief      Write user-specified data in batches to user-specified registers (only registers with 4-byte command values)
			After the current interface is invoked, the node value obtained by using interfaces such as GXGetEnum, GXGetInt, and GXGetBool is the previous value
			The latest register values can be obtained using the GXReadPort interface
\param		[in]hPort           The handle
\param		[in]pstEntries      [in]Batch write register addresses and values
\param		[in|out]piSize      [in]Sets the number of device registers
								[out]The number of successful register writes
\return     GX_STATUS_SUCCESS				The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API			The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_HANDLE		The illegal handle that the user introduces, or reclose the device.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXWritePortStacked(GX_PORT_HANDLE hPort, const GX_REGISTER_STACK_ENTRY* pstEntries, size_t *piSize);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief       ��ȡ���Ű���ֵ
\param		[in]hDevice				�豸���
\param		[out]punPacketSize		���Ű���ֵ
\return     GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_TIMEOUT             ������ʱ
			GC_ERR_IO                     IOͨѶ����
			GX_STATUS_INVALID_PARAMETER   ��Ч����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			GX_STATUS_NOT_IMPLEMENTED     ��ǰ��֧�ֵĹ���
			����û�к��ǵ��ģ��������Ĵ��������μ�GX_STATUS_LIST

\English:
\brief      Gets the optimal packet length value
\param		[in]hDevice				The handle of the device.
\param		[out]punPacketSize		Optimal packet length
\return     GX_STATUS_SUCCESS             The operation is successful and no error occurs.
			GX_STATUS_TIMEOUT             Operation timeout
			GC_ERR_IO                     IO communication error
			GX_STATUS_INVALID_PARAMETER   The input parameter that the user introduces is invalid.
			GX_STATUS_INVALID_HANDLE      The illegal handle that the user introduces, or reclose the device.
			GX_STATUS_NOT_IMPLEMENTED     Currently unsupported features
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API  GXGetOptimalPacketSize (GX_DEV_HANDLE hDevice, uint32_t* punPacketSize);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      �û����òɼ�buffer����
\param		[in]hDevice         �豸���
\param		[in]nBufferNum      �û����õ�buffer����
\return     GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			GX_STATUS_INVALID_PARAMETER    ���������Ч
			����û�к��ǵ��ģ��������Ĵ��������μ�GX_STATUS_LIST

\English:
\brief      Set the number of the acquisition buffers.
\param		[in]hDevice         The handle of the device.
\param		[in]nBufferNum      The number of the acquisition buffers that the user sets.
\return     GX_STATUS_SUCCESS             The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API        The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_HANDLE      The handle that the user introduces is illegal.
			GX_STATUS_INVALID_PARAMETER    The input parameter that the user introduces is invalid.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXSetAcqusitionBufferNumber(GX_DEV_HANDLE hDevice, uint64_t nBufferNum);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ע��ɼ��ص�����
\attention  �����ڷ��Ϳ�������֮ǰע��ɼ��ص�����
\param		[in]hDevice			�豸���
\param		[in]pUserParam		�û�˽������
\param		[in]callBackFun		�û�ע��Ļص�����
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			GX_STATUS_INVALID_PARAMETER   �û�����ָ��ΪNULL
			GX_STATUS_INVALID_CALL        ���Ϳ�������󣬲���ע��ɼ��ص�����
			�������������μ�GX_STATUS_LIST

\English:
\brief      Register the capture callback function
\attention  The collection callback function must be registered before the mining command is sent
\param		[in]hDevice			The handle of the device.
\param		[in]pUserParam		User private data
\param		[in]callBackFun		User registration callback function
\return		GX_STATUS_SUCCESS             The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API        The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_HANDLE      The handle that the user introduces is illegal.
			GX_STATUS_INVALID_PARAMETER   The input parameter that the user introduces is invalid.
			GX_STATUS_INVALID_CALL        After sending the start acquisition command, the user can not register the capture callback function.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXRegisterCaptureCallback  (GX_DEV_HANDLE hDevice, void *pUserParam, GXCaptureCallBack callBackFun);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ע���ɼ��ص�����
\attention  �����ڷ���ͣ������֮��ע���ɼ��ص�����
\param		[in]hDevice				�豸���
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			GX_STATUS_INVALID_CALL        ����ͣ������֮ǰ������ע���ɼ��ص�����
			�������������μ�GX_STATUS_LIST

\English:
\brief      Unregister the capture callback function
\attention  The collection callback function must be deregistered after the stop mining command is sent
\param		[in]hDevice				The handle of the device.
\return		GX_STATUS_SUCCESS             The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API        The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_HANDLE      The handle that the user introduces is illegal.
			GX_STATUS_INVALID_CALL        The collection callback function cannot be deregistered before sending the stop mining command
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXUnregisterCaptureCallback(GX_DEV_HANDLE hDevice);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ֱ�ӻ�ȡһ֡ͼ��
\attention  ����û���ע��ɼ��ص����������ô˽ӿڻᱨ��GX_STATUS_INVALID_CALL
\param		[in]hDevice        �豸���
\param		[in|out]pFrameData ͼ����Ϣ�ṹ��ָ��
\param		[in]nTimeout       ��ʱʱ��
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			GX_STATUS_INVALID_CALL        ����ͣ������֮ǰ������ע���ɼ��ص�����
			GX_STATUS_INVALID_PARAMETER   �û�����ͼ���ַָ��ΪNULL
			GX_STATUS_NEED_MORE_BUFFER    �û������ͼ��bufferС��ʵ����Ҫ�Ĵ�С
			�������������μ�GX_STATUS_LIST

\English:
\brief      Get a frame of image directly
\attention  If the user has registered the collection callback function, calling this interface results in an error GX_STATUS_INVALID_CALL
\param		[in]hDevice			The handle of the device.
\param		[in|out]pFrameData	Pointer to image information structure
\param		[in]nTimeout		Timeout period
\return		GX_STATUS_SUCCESS             The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API        The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_HANDLE      The handle that the user introduces is illegal.
			GX_STATUS_INVALID_CALL        After registering the capture callback function, the user calls the GXGetImage to get image.
			GX_STATUS_INVALID_PARAMETER   User incoming image address pointer is NULL.
			GX_STATUS_NEED_MORE_BUFFER    The image buffer assigned by the user is smaller than it needs to be
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXGetImage(GX_DEV_HANDLE hDevice, GX_FRAME_DATA *pFrameData, uint32_t nTimeout);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ղɼ��������
\attention  ����û�����ͼ����ٶȽ��������ڻ�����ϴβɼ����̵Ļ���ͼ���ر��ڴ���ģʽ�£�
            �û������괥��֮�󣬻�ȡ�����Ǿ�ͼ������û����ȡ����ǰ������Ӧ��ͼ����Ҫ��
		    ���ʹ���֮ǰ����GXFlushQueue�ӿڣ������ͼ��������С�
\param		[in]hDevice        �豸���
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			�������������μ�GX_STATUS_LIST

\English:
\brief      Clear the collection output queue
\attention  If the user processes the images too slow, the image of last acquisition may be remained in the queue.
			Especially in the trigger mode, after the user send the trigger signal, and get the old image (last image).
			If you want to get the current image that corresponding to trigger signal, you should call the
			GXFlushQueue interface before sending the trigger signal to empty the image output queue.
\param		[in]hDevice				The handle of the device.
\return		GX_STATUS_SUCCESS             The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API        The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_HANDLE      The handle that the user introduces is illegal.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXFlushQueue(GX_DEV_HANDLE hDevice);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ע���¼��ص�����
\attention  �豸�¼����磬�����¼����ع�����ȣ���Щ�¼�������ͨ������ӿڵĻص���ʽ������
            �û�����Ҫ��ȡ�¼���ʱ�����GXUnregisterEventCallback�ӿ�ע���ص�����
\param		[in]hDevice			�豸���
\param		[in]pUserParam		�û�˽������
\param		[in]callBackFun		�û�ע��Ļص�����
\param		[out]pHCallBack		���߻ص�����������˾������ע���ص�����ʹ��
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			GX_STATUS_INVALID_PARAMETER   �û�����ص������Ƿ����ߴ����¼����ͷǷ�
			�������������μ�GX_STATUS_LIST

\English:
\brief      Register the event callback function
\attention  Exposure device events, for example, offline events, such as end, these events can be covered by the interface mode of the callback,
			users do not need to access the event called when GXUnregisterEventCallback interface cancelled the callback function
\param		[in]hDevice			The handle of the device.
\param		[in]pUserParam		User private parameter.
\param		[in]callBackFun		User registration callback function
\param		[out]pHCallBack			The handle of offline callback function, the handle is used for unregistering the callback function.
\return		GX_STATUS_SUCCESS             The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API        The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_HANDLE      The handle that the user introduces is illegal.
			GX_STATUS_INVALID_PARAMETER   The callback function or event type passed by the user is invalid
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXRegisterDeviceOfflineCallback    (GX_DEV_HANDLE hDevice,
										   void* pUserParam,
										   GXDeviceOfflineCallBack callBackFun,
										   GX_EVENT_CALLBACK_HANDLE *pHCallBack);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ע���¼��ص�����
\attention  �豸�¼����磬�����¼����ع�����ȣ���Щ�¼�������ͨ������ӿڵĻص���ʽ������
            �û�����Ҫ��ȡ�¼���ʱ�����GXUnregisterEventCallback�ӿ�ע���ص�����
\param		[in]hDevice			�豸���
\param		[in]pHCallBack		���߻ص��������
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			GX_STATUS_INVALID_CALL        ����ͣ������֮ǰ������ע���ɼ��ص�����
			GX_STATUS_INVALID_PARAMETER   �û������¼����ͷǷ�
			�������������μ�GX_STATUS_LIST

\English:
\brief      Unregister event handle callback function
\attention  Device events such as drop events, end of exposure, etc., these events can be transmitted through the callback mode of this interface.
			Called when users do not need to obtain events GXUnregisterEventCallback interface cancelled the callback function
\param		[in]hDevice				The handle of the device.
\param		[in]pHCallBack			The handle of offline callback function
\return		GX_STATUS_SUCCESS             The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API        The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_HANDLE      The handle that the user introduces is illegal.
			GX_STATUS_INVALID_CALL        The collection callback function cannot be deregistered before sending the stop mining command
			GX_STATUS_INVALID_PARAMETER   The callback function or event type passed by the user is invalid
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXUnregisterDeviceOfflineCallback  (GX_DEV_HANDLE hDevice, GX_EVENT_CALLBACK_HANDLE  hCallBack);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ����¼��������
\attention  ���ڲ��¼����ݵĽ��պʹ������û�����ƣ�����û����ա������¼����ٶ������¼��������ٶȣ�
            �¼����ݾͻ��ڿ��ڻ��ۣ���Ӱ���û���ȡʵʱ�¼����ݡ�����û����ȡʵʱ�¼����ݣ���Ҫ��
			����GXFlushEvent�ӿ�����¼��������ݡ��˽ӿ�һ������������¼����ݡ�
\param		[in]hDevice        �豸���
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			�������������μ�GX_STATUS_LIST

\English:
\brief      Empty the event output queue
\attention  The library internal event data receiving and processing using caching mechanism, if the user
			receiving, processing event speed is slower than the event generates, then the event data will be
			accumulated in the library, it will affect the the user to get real-time event data. If you want to get
			the real-time event data, you need to call the GXFlushEvent interface to clear the event cache data.
			This interface empties all the event data at once.
\param		[in]hDevice			The handle of the device.
\return		GX_STATUS_SUCCESS             The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API        The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_HANDLE      The handle that the user introduces is illegal.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXFlushEvent               (GX_DEV_HANDLE hDevice);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief     ��ȡ��ǰ�¼�����������¼�����
\param     [in]hDevice        �豸���
\param     [in]pnEventNum     �¼�����ָ��
\return    GX_STATUS_SUCCESS             �����ɹ���û�з�������
		   GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
           GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
           GX_STATUS_INVALID_PARAMETER   �û�����pnEventNumΪNULLָ��
           �������������μ�GX_STATUS_LIST

\English:
\brief     Get the number of the events in the current remote device event queue cache.
\param     [in]hDevice        The handle of the device.
\param     [in]pnEventNum     The pointer of event number.
\return    GX_STATUS_SUCCESS             The operation is successful and no error occurs.
		   GX_STATUS_NOT_INIT_API        The GXInitLib initialization library is not called.
		   GX_STATUS_INVALID_HANDLE      The handle that the user introduces is illegal.
		   GX_STATUS_INVALID_PARAMETER   The pointer that the user input is NULL.
		   The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXGetEventNumInQueue       (GX_DEV_HANDLE hDevice, uint32_t *pnEventNum);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ע�����Ը��»ص�����
\attention  �û���ͨ���˽ӿڻ�ȡ�¼����ݣ����ʾ������
\param		[in]hDevice          �豸���
\param		[in]pUserParam       �û�˽������
\param		[in]callBackFun      �û�ע��Ļص�����
\param		[in]strfeatureName   �ڵ�����
\param		[out]pHCallBack      �ص��������
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			GX_STATUS_INVALID_PARAMETER   �û�����ص������Ƿ�
			�������������μ�GX_STATUS_LIST

\English:
\brief      Register property update callback function.
\attention  Users can obtain event data through this interface, as described in the example program
\param		[in]hDevice          The handle of the device
\param		[in]pUserParam       User Private Parameters
\param		[in]callBackFun      User registration callback function
\param		[in]strfeatureName   Node name
\param		[out]pHCallBack      Callback function handle
\return		GX_STATUS_SUCCESS             The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API        The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_HANDLE      The handle that the user introduces is illegal.
			GX_STATUS_INVALID_PARAMETER   The pointer that the user input is NULL.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXRegisterFeatureCallbackByString  (GX_PORT_HANDLE hPort,
								   void* pUserParam,
								   GXFeatureCallBackByString  callBackFun,
								   const char*  strfeatureName,
								   GX_FEATURE_CALLBACK_BY_STRING_HANDLE *pHCallBack);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ע�����Ը��»ص�����
\attention  ��GXRegisterFeatureCallback����ʹ�ã�ÿ��ע�ᶼ��������Ӧ��ע����֮��Ӧ
\param		[in]hDevice				�豸���
\param		[in]strfeatureName		�ڵ�����
\param		[out]pHCallBack			�ص��������
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			�������������μ�GX_STATUS_LIST

\English:
\brief      Unregister device attribute update callback function.
\attention  Used in conjunction with GXRegisterFeatureCallback, each registration must have a corresponding cancellation corresponding to it.
\param		[in]hDevice				The handle of the device
\param		[in]strfeatureName		Node name
\param		[out]pHCallBack			Callback function handle
\return		GX_STATUS_SUCCESS             The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API        The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_HANDLE      The handle that the user introduces is illegal.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXUnregisterFeatureCallbackByString(GX_PORT_HANDLE  hPort,
                                    const char* strfeatureName,
                                    GX_FEATURE_CALLBACK_BY_STRING_HANDLE  hCallBack);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ���������ǰ�����������ļ���ANSI�ӿڣ�
\param		[in]hDevice         �豸���
\param		[in]pszFilePath     �����ļ����·��
\return     GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			����û�к��ǵ��ģ��������Ĵ��������μ�GX_STATUS_LIST

\English:
\brief      Export the current parameter of the camera to the configuration file.(ANSI)
\param      [in]hDevice             The handle of the device.
\param      [in]pszFilePath         The path of the configuration file that to be generated.
\return     GX_STATUS_SUCCESS               The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API          The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_HANDLE        The handle that the user introduces is illegal.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXExportConfigFile (GX_DEV_HANDLE hDevice, const char * pszFilePath);


//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ���������ǰ�����������ļ���UNICODE�ӿڣ�
\param		[in]hDevice         �豸���
\param		[in]pszFilePath     �����ļ����·����wchar_t���ͣ�
\return     GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			����û�к��ǵ��ģ��������Ĵ��������μ�GX_STATUS_LIST

\English:
\brief		(unicode interface)
\param		[in]hDevice
\param		[in]pszFilePath
\return     GX_STATUS_SUCCESS
			GX_STATUS_NOT_INIT_API
			GX_STATUS_INVALID_HANDLE
			Other status refer to GX_STATUS_LIST
*/
//----------------------------------------------------------------------------------
GX_API GXExportConfigFileW(GX_DEV_HANDLE hDevice, const wchar_t * pszFilePath);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      �������ļ��в������뵽�����ANSI�ӿڣ�
\param		[in]hDevice         �豸���
\param		[in]pszFilePath     �����ļ�·��
\param		[in]bVerify         �����ֵΪtrue�����е����ȥ��ֵ���ᱻ��������У���Ƿ�һ��
\return     GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			����û�к��ǵ��ģ��������Ĵ��������μ�GX_STATUS_LIST

\English:
\brief      Import the configuration file for the camera.(ANSI)
\param      [in]hDevice             The handle of the device.
\param      [in]pszFilePath         The path of the configuration file.
\param      [in]bVerify             If bVerify is true, all imported values will be read out to verify consistency.
\return     GX_STATUS_SUCCESS               The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API          The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_HANDLE        The handle that the user introduces is illegal.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
#ifndef __cplusplus
GX_API GXImportConfigFile(GX_DEV_HANDLE hDevice, const char * pszFilePath, bool bVerify);
#else
GX_API GXImportConfigFile(GX_DEV_HANDLE hDevice, const char * pszFilePath, bool bVerify = false);
#endif

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      �������ļ��в������뵽�����UNICODE�ӿڣ�
\param		[in]hDevice         �豸���
\param		[in]pszFilePath     �����ļ�·��
\param		[in]bVerify         �����ֵΪtrue�����е����ȥ��ֵ���ᱻ��������У���Ƿ�һ��
\return     GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			����û�к��ǵ��ģ��������Ĵ��������μ�GX_STATUS_LIST

\English:
\brief		(unicode interface)
\param		[in]hDevice
\param		[in]pszFilePath
\param		[in]bVerify
\return     GX_STATUS_SUCCESS
			GX_STATUS_NOT_INIT_API
			GX_STATUS_INVALID_HANDLE
			Other status refer to GX_STATUS_LIST
*/
//----------------------------------------------------------------------------------
#ifndef __cplusplus
GX_API GXImportConfigFileW(GX_DEV_HANDLE hDevice, const wchar_t * pszFilePath, bool bVerify);
#else
GX_API GXImportConfigFileW(GX_DEV_HANDLE hDevice, const wchar_t * pszFilePath, bool bVerify = false);
#endif

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      �ɼ�֮���ͨ���˽ӿڻ�ȡһ��ͼƬ
\attention  ��
\param		[in]hDevice    �豸���
\param      [out]ppFrameBuffer      ���Ż� GxIAPI ���ͼ������ Buf ָ��
\param      [in]nTimeOut            ��ʱʱ�䵥Ԫ����
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			GX_STATUS_NOT_IMPLEMENTED     ��ǰ��֧�ֵĹ���
			GX_STATUS_ERROR_TYPE          �û������featureID���ʹ���
			GX_STATUS_INVALID_ACCESS      ��ǰ���ɷ���
			�������������μ�GX_STATUS_LIST

\English:
\brief      After starting the acquisition, an image (zero copy) can be acquired through this interface.
\param      [in]hDevice             The handle of the device.
\param      [out]ppFrameBuffer      Address pointer of image data output by the interface.
\param      [in]nTimeOut            Take timeout time (unit: ms).
\return     GX_STATUS_SUCCESS               The operation is successful and no error occurs.
            GX_STATUS_NOT_INIT_API          The GXInitLib initialization library is not called.
            GX_STATUS_INVALID_HANDLE        The handle that the user introduces is illegal.
            GX_STATUS_INVALID_PARAMETER     The pointer that the user introduces is NULL.
            GX_STATUS_INVALID_CALL          Acquisition is not started or the callback is registered. It
                                            is not allowed to call the interface.
            GX_STATUS_TIMEOUT               Acquire image timeout error.
            GX_STATUS_ERROR                 Unspecified internal errors that are not expected to occur.
            The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXDQBuf (GX_DEV_HANDLE hDevice,
                PGX_FRAME_BUFFER *ppFrameBuffer,
                uint32_t nTimeOut);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      �ڿ�ʼ�ɼ�֮��ͨ���˽ӿڿ��Խ�ͼ������ Buf �Ż� GxIAPI �⣬�������ڲɼ���
\attention  ��
\param		[in]hDevice    �豸���
\param      [out]ppFrameBuffer      ���Ż� GxIAPI ���ͼ������ Buf ָ��
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			GX_STATUS_NOT_IMPLEMENTED     ��ǰ��֧�ֵĹ���
			GX_STATUS_ERROR_TYPE          �û������featureID���ʹ���
			GX_STATUS_INVALID_ACCESS      ��ǰ���ɷ���
			�������������μ�GX_STATUS_LIST

\English:
\brief      After the acquisition is started, the image data buffer can be placed back into the GxIAPI
            library through this interface and continue to be used for acquisition.
\param      [in]hDevice             The handle of the device.
\param      [in]pFrameBuffer        Image data buffer pointer to be placed back into the GxIAPI library.
\return     GX_STATUS_SUCCESS               The operation is successful and no error occurs.
            GX_STATUS_NOT_INIT_API          The GXInitLib initialization library is not called.
            GX_STATUS_INVALID_HANDLE        The handle that the user introduces is illegal.
            GX_STATUS_INVALID_PARAMETER     The pointer that the user introduces is NULL.
            GX_STATUS_INVALID_CALL          Acquisition is not started or the callback is registered. It
                                           is not allowed to call the interface.
            The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXQBuf (GX_DEV_HANDLE hDevice, PGX_FRAME_BUFFER pFrameBuffer);

//----------------------------------------------------------------------------------
/**
\brief    Register the extern buffer for grab.
\return   void
*/
//----------------------------------------------------------------------------------

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ע�Ỻ����
\param		[in]hDevice			   		  �豸handle
\param		[in]pBuffer		         	  ע��buffer�е�pBuffer
\param		[in]nSize      	    	  	  ע��buffer�Ĵ�С
\param		[in]pUserParam      	      �û�����
\return     GX_STATUS_SUCCESS             �����ɹ���û�з�������
            GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
            GX_STATUS_INVALID_PARAMETER   �û������ָ��ΪNULL
            �������������μ�GX_STATUS_LIST

\English:
\brief      Register the extern buffer.
\param		[in]hDevice			   		  The handle of the device
\param		[in]pBuffer      	    	  The Register Buffer
\param		[in]nSize      	    	  	  The Register Buffer size
\param		[in]pUserParam      	      The User Parameter
\return     GX_STATUS_SUCCESS             The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API        The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER   The pointer that the user input is NULL.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXRegisterBuffer(GX_DEV_HANDLE hDevice, void* pBuffer, size_t nSize, void* pUserParam);

// ---------------------------------------------------------------------------
/**
\Chinese��
\brief      ע��������
\param		[in]hDevice			   		�豸handle
\param		[in]pBuffer      	     ע��buffer�е�pBuffer
\return     GX_STATUS_SUCCESS             �����ɹ���û�з�������
            GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
            GX_STATUS_INVALID_PARAMETER   �û������ָ��ΪNULL
            �������������μ�GX_STATUS_LIST

\English:
\brief      Unregister the extern buffer.
\param		[in]hDevice			   		The handle of the device
\param		[in]pBuffer 	     	    The RegisterBuffer pBuffer
\return     GX_STATUS_SUCCESS             The operation is successful and no error occurs.
			GX_STATUS_NOT_INIT_API        The GXInitLib initialization library is not called.
			GX_STATUS_INVALID_PARAMETER   The pointer that the user input is NULL.
			The errors that are not covered above please reference GX_STATUS_LIST.
*/
// ---------------------------------------------------------------------------
GX_API GXUnRegisterBuffer(GX_DEV_HANDLE hDevice, void* pBuffer);

#ifndef _WIN32
//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ��ʼ�ɼ��������ɼ����豸�ɼ�
\attention  ��
\param		[in]hDevice    �豸���
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			GX_STATUS_NOT_IMPLEMENTED     ��ǰ��֧�ֵĹ���
			GX_STATUS_ERROR_TYPE          �û������featureID���ʹ���
			GX_STATUS_INVALID_ACCESS      ��ǰ���ɷ���
			�������������μ�GX_STATUS_LIST 

\English:
\brief      Start acquisition, including stream acquisition and device acquisition.
\param      [in]hDevice             The handle of the device.
\return     GX_STATUS_SUCCESS               The operation is successful and no error occurs.
            GX_STATUS_NOT_INIT_API          The GXInitLib initialization library is not called.
            GX_STATUS_INVALID_HANDLE        The handle that the user introduces is illegal.
            GX_STATUS_INVALID_ACCESS        Device access mode error.
            GX_STATUS_ERROR                 Unspecified internal errors that are not expected to occur.
            The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXStreamOn (GX_DEV_HANDLE hDevice);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      ֹͣ�ɼ��������ɼ����豸�ɼ�
\attention  ��
\param		[in]hDevice    �豸���
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			GX_STATUS_NOT_IMPLEMENTED     ��ǰ��֧�ֵĹ���
			GX_STATUS_ERROR_TYPE          �û������featureID���ʹ���
			GX_STATUS_INVALID_ACCESS      ��ǰ���ɷ���
			�������������μ�GX_STATUS_LIST 

\English:
\brief      Stop acquisition, including stop stream acquisition and stop device acquisition.
\param      [in]hDevice             The handle of the device.
\return     GX_STATUS_SUCCESS               The operation is successful and no error occurs.
            GX_STATUS_NOT_INIT_API          The GXInitLib initialization library is not called.
            GX_STATUS_INVALID_HANDLE        The handle that the user introduces is illegal.
            GX_STATUS_INVALID_ACCESS        Device access mode error.
            GX_STATUS_INVALID_CALL          Acquisition is not started or the callback is registered. It
                                            is not allowed to call the interface.
            GX_STATUS_ERROR                 Unspecified internal errors that are not expected to occur.
            The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXStreamOff (GX_DEV_HANDLE hDevice);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      �ڿ�ʼ�ɼ�֮��ͨ���˽ӿڿ��Ի�ȡ���вɼ���ͼ��� buf���㿽��������ȡ����ͼ�����������е�
��ͼ˳���ǴӾɵ��£��� ppFrameBufferArray[0]�洢������ɵ�ͼ��ppFrameBufferArray[nFrameCount -
1]�洢�������µ�ͼ��
\attention  ��
\param		[in]hDevice    �豸���
\param      [out]ppFrameBufferArray         ͼ������ָ�������
\param      [in]nFrameBufferArraySize       ͼ�������������
\param      [out]pnFrameCount               ����ʵ�����ͼ�����
\param      [in]nTimeOut                    ȡͼ�ĳ�ʱʱ�䣨��λ ms��
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			GX_STATUS_NOT_IMPLEMENTED     ��ǰ��֧�ֵĹ���
			GX_STATUS_ERROR_TYPE          �û������featureID���ʹ���
			GX_STATUS_INVALID_ACCESS      ��ǰ���ɷ���
			�������������μ�GX_STATUS_LIST 

\English:
\brief      After starting the acquisition, all the buffers (zero copies) of the acquired images can be
            obtained through this interface. The order of the stored images in the image data array is
            from old to new, that is, ppFrameBufferArray[0] stores the oldest image, and
            ppFrameBufferArray[nFrameCount - 1] stores the latest image.
\param      [in]hDevice                     The handle of the device.
\param      [out]ppFrameBufferArray         Array of image data pointers.
\param      [in]nFrameBufferArraySize       The number of applications for image arrays.
\param      [out]pnFrameCount               Returns the number of actual filled images.
\param      [in]nTimeOut                    Take timeout time (unit: ms).
\return     GX_STATUS_SUCCESS               The operation is successful and no error occurs.
            GX_STATUS_NOT_INIT_API          The GXInitLib initialization library is not called.
            GX_STATUS_INVALID_HANDLE        The handle that the user introduces is illegal.
            GX_STATUS_INVALID_PARAMETER     The pointer that the user introduces is NULL.
            GX_STATUS_INVALID_CALL          Acquisition is not started or the callback is registered. It
                                            is not allowed to call the interface.
            GX_STATUS_NEED_MORE_BUFFER      Insufficient buffer requested by the user: When reading,the user
                                            input buffer size is smaller than the actual need.
            GX_STATUS_TIMEOUT               Acquire image timeout error.
            GX_STATUS_ERROR                 Unspecified internal errors that are not expected to occur.
            The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXDQAllBufs (GX_DEV_HANDLE hDevice, 
                    PGX_FRAME_BUFFER *ppFrameBufferArray, 
                    uint32_t nFrameBufferArraySize, 
                    uint32_t *pnFrameCount,
                    uint32_t nTimeOut);

//----------------------------------------------------------------------------------
/**
\Chinese��
\brief      �ڿ�ʼ�ɼ�֮��ͨ���˽ӿڿ��Խ�ͼ������ Buf �Ż� GxIAPI �⣬�������ڲɼ���
\attention  ��
\param		[in]hDevice    �豸���
\return		GX_STATUS_SUCCESS             �����ɹ���û�з�������
			GX_STATUS_NOT_INIT_API        û�е���GXInitLib��ʼ����
			GX_STATUS_INVALID_HANDLE      �û�����Ƿ��ľ��
			GX_STATUS_NOT_IMPLEMENTED     ��ǰ��֧�ֵĹ���
			GX_STATUS_ERROR_TYPE          �û������featureID���ʹ���
			GX_STATUS_INVALID_ACCESS      ��ǰ���ɷ���
			�������������μ�GX_STATUS_LIST 

\English:
\brief      After the acquisition is started, all the acquired image data buffers can be put back into
            the GxIAPI library through this interface, and continue to be used for acquisition.
\param      [in]hDevice             The handle of the device.
\return     GX_STATUS_SUCCESS               The operation is successful and no error occurs.
            GX_STATUS_NOT_INIT_API          The GXInitLib initialization library is not called.
            GX_STATUS_INVALID_HANDLE        The handle that the user introduces is illegal.
            GX_STATUS_INVALID_CALL          Invalid interface call.
            The errors that are not covered above please reference GX_STATUS_LIST.
*/
//----------------------------------------------------------------------------------
GX_API GXQAllBufs (GX_DEV_HANDLE hDevice);
#endif

#endif  //GX_GALAXY_H
