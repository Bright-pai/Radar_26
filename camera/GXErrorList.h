

#ifndef GX_ERROR_LIST_H
#define GX_ERROR_LIST_H


//////////////////////////////////////////////////////////////////////////
//Chinese��	���Ͷ��壬�������Ͷ��ڱ�׼C��ͷ�ļ�stdint.h���ж��壬������΢���ı���ƽ̨
//			VS2010֮ǰ�İ汾�ж����������ļ�,�����ڴ���Ҫ�ض���

//English:	The following types are defined in the standard C library header file stdint.h, but are available on Microsoft's compilation platform
//			This file was not included in previous versions of VS2010, so it needs to be redefined here
//////////////////////////////////////////////////////////////////////////

#if defined(_WIN32)
	#ifndef _STDINT_H 
		#ifdef _MSC_VER // Microsoft compiler
			#if _MSC_VER < 1600
				typedef __int8            int8_t;
				typedef __int16           int16_t;
				typedef __int32           int32_t;
				typedef __int64           int64_t;
				typedef unsigned __int8   uint8_t;
				typedef unsigned __int16  uint16_t;
				typedef unsigned __int32  uint32_t;
				typedef unsigned __int64  uint64_t;
			#else
				// In Visual Studio 2010 is stdint.h already included
				#include <stdint.h>
			#endif
		#else
			// Not a Microsoft compiler
			#include <stdint.h>
		#endif
	#endif 
#else
	// Linux
	#include <stdint.h>
#endif

//------------------------------------------------------------------------------
//Chinese�� �����붨��
//English:	Error code definition
//------------------------------------------------------------------------------
typedef enum GX_STATUS_LIST
{
	GX_STATUS_SUCCESS                =  0,           ///< \Chinese	�ɹ�															\English	Success
	GX_STATUS_ERROR					 = -1,           ///< \Chinese	������������δ��ȷָ�����ڲ�����								\English	There is an unspecified internal error that is not expected to occur
	GX_STATUS_NOT_FOUND_TL           = -2,           ///< \Chinese	�Ҳ���TL��													\English	The TL library cannot be found
	GX_STATUS_NOT_FOUND_DEVICE       = -3,           ///< \Chinese	�Ҳ����豸													\English	The device is not found
	GX_STATUS_OFFLINE                = -4,           ///< \Chinese	��ǰ�豸Ϊ����״̬											\English	The current device is in an offline status
	GX_STATUS_INVALID_PARAMETER		 = -5,           ///< \Chinese	��Ч����,һ����ָ��ΪNULL�������IP�Ȳ�����ʽ��Ч				\English	Invalid parameter. Generally, the pointer is NULL or the input IP and other parameter formats are invalid
	GX_STATUS_INVALID_HANDLE         = -6,           ///< \Chinese	��Ч���														\English	Invalid handle
	GX_STATUS_INVALID_CALL			 = -7,           ///< \Chinese	��Ч�Ľӿڵ���,רָ�����ӿ��߼�����								\English	The interface is invalid, which refers to software interface logic error
	GX_STATUS_INVALID_ACCESS		 = -8,           ///< \Chinese	���ܵ�ǰ���ɷ��ʻ��豸����ģʽ����								\English	The function is currently inaccessible or the device access mode is incorrect
	GX_STATUS_NEED_MORE_BUFFER		 = -9,           ///< \Chinese	�û������buffer����:������ʱ�û�����buffersizeС��ʵ����Ҫ		\English	The user request buffer is insufficient: the user input buffersize during the read operation is less than the actual need
	GX_STATUS_ERROR_TYPE			 = -10,          ///< \Chinese	�û�ʹ�õ�FeatureID���ʹ��󣬱������ͽӿ�ʹ���˸����͵Ĺ�����		\English	The type of FeatureID used by the user is incorrect, such as an integer interface using a floating - point function code
	GX_STATUS_OUT_OF_RANGE           = -11,          ///< \Chinese	�û�д���ֵԽ��												\English	The value written by the user is crossed
	GX_STATUS_NOT_IMPLEMENTED        = -12,          ///< \Chinese	��ǰ��֧�ֵĹ���												\English	This function is not currently supported
	GX_STATUS_NOT_INIT_API           = -13,          ///< \Chinese	û�е��ó�ʼ���ӿ�											\English	There is no call to initialize the interface
	GX_STATUS_TIMEOUT                = -14,          ///< \Chinese	��ʱ����														\English	Timeout error
}GX_STATUS_LIST;
typedef int32_t GX_STATUS;


#endif //GX_ERROR_LIST_H