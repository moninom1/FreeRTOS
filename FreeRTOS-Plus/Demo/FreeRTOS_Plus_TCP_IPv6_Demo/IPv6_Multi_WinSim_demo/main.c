/*
 * FreeRTOS+TCP V2.3.2
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 *
 * 1 tab == 4 spaces!
 */

/*
 * This project is a cut down version of the project described on the following
 * link.  Only the simple UDP client and server and the TCP echo clients are
 * included in the build:
 * http://www.freertos.org/FreeRTOS-Plus/FreeRTOS_Plus_TCP/examples_FreeRTOS_simulator.html
 */

/* Standard includes. */
#include <stdio.h>
#include <time.h>

/* FreeRTOS includes. */
#include <FreeRTOS.h>
#include "task.h"
#include "semphr.h"

/* Demo application includes. */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_Routing.h"

#if ( ipconfigUSE_NTP_DEMO != 0 )
#include "NTPDemo.h"
#endif

#if ( ipconfigMULTI_INTERFACE == 1 )
    #include "FreeRTOS_ND.h"
#endif

#include "logging.h"

#include "plus_tcp_demo_cli.h"
#include "TCPEchoClient_SingleTasks.h"

#ifndef ARRAY_SIZE
    #define ARRAY_SIZE( x )    ( int ) ( sizeof( x ) / sizeof( x )[ 0 ] )
#endif

#ifdef ipconfigCOMPATIBLE_WITH_SINGLE
#undef ipconfigCOMPATIBLE_WITH_SINGLE
#endif

/* Simple UDP client and server task parameters. */
#define mainSIMPLE_UDP_CLIENT_SERVER_TASK_PRIORITY    ( tskIDLE_PRIORITY )
#define mainSIMPLE_UDP_CLIENT_SERVER_PORT             ( 5005UL )

/* Echo client task parameters - used for both TCP and UDP echo clients. */
#define mainECHO_CLIENT_TASK_STACK_SIZE               ( configMINIMAL_STACK_SIZE * 2 )      /* Not used in the Windows port. */
#define mainECHO_CLIENT_TASK_PRIORITY                 ( tskIDLE_PRIORITY + 1 )

/* Echo server task parameters. */
#define mainECHO_SERVER_TASK_STACK_SIZE               ( configMINIMAL_STACK_SIZE * 2 )      /* Not used in the Windows port. */
#define mainECHO_SERVER_TASK_PRIORITY                 ( tskIDLE_PRIORITY + 1 )

/* Define a name that will be used for LLMNR and NBNS searches. */
#define mainHOST_NAME                                 "RTOSDemo"
#define mainDEVICE_NICK_NAME                          "windows_demo"

/* Set the following constants to 1 or 0 to define which tasks to include and
 * exclude:
 *
 * mainCREATE_SIMPLE_UDP_CLIENT_SERVER_TASKS:  When set to 1 two UDP client tasks
 * and two UDP server tasks are created.  The clients talk to the servers.  One set
 * of tasks use the standard sockets interface, and the other the zero copy sockets
 * interface.  These tasks are self checking and will trigger a configASSERT() if
 * they detect a difference in the data that is received from that which was sent.
 * As these tasks use UDP, and can therefore loose packets, they will cause
 * configASSERT() to be called when they are run in a less than perfect networking
 * environment.
 *
 * mainCREATE_TCP_ECHO_TASKS_SINGLE:  When set to 1 a set of tasks are created that
 * send TCP echo requests to the standard echo port (port 7), then wait for and
 * verify the echo reply, from within the same task (Tx and Rx are performed in the
 * same RTOS task).  The IP address of the echo server must be configured using the
 * configECHO_SERVER_ADDR0 to configECHO_SERVER_ADDR3 constants in
 * FreeRTOSConfig.h.
 *
 * mainCREATE_TCP_ECHO_SERVER_TASK:  When set to 1 a task is created that accepts
 * connections on the standard echo port (port 7), then echos back any data
 * received on that connection.
 */
#define mainCREATE_SIMPLE_UDP_CLIENT_SERVER_TASKS     0
#define mainCREATE_TCP_ECHO_TASKS_SINGLE              1 /* 1 */
#define mainCREATE_TCP_ECHO_SERVER_TASK               0
/*-----------------------------------------------------------*/

/* Define a task that is used to start and monitor several tests. */
static void prvServerWorkTask( void * pvArgument );

/* Let this task run at a low priority. */
#define mainTCP_SERVER_TASK_PRIORITY    ( tskIDLE_PRIORITY + 1 )

/* Give it an appropriate stack size. */
#define mainTCP_SERVER_STACK_SIZE       2048

/*
 * Just seeds the simple pseudo random number generator.
 */
static void prvSRand( UBaseType_t ulSeed );

/*
 * Miscellaneous initialisation including preparing the logging and seeding the
 * random number generator.
 */
static void prvMiscInitialisation( void );
static void dns_test(const char* pcHostName);

void showAddressInfo(struct freertos_addrinfo* pxAddrInfo);

/* The default IP and MAC address used by the demo.  The address configuration
 * defined here will be used if ipconfigUSE_DHCP is 0, or if ipconfigUSE_DHCP is
 * 1 but a DHCP server could not be contacted.  See the online documentation for
 * more information. */
static const uint8_t ucIPAddress[ 4 ] = { configIP_ADDR0, configIP_ADDR1, configIP_ADDR2, configIP_ADDR3 };
static const uint8_t ucNetMask[ 4 ] = { configNET_MASK0, configNET_MASK1, configNET_MASK2, configNET_MASK3 };
static const uint8_t ucGatewayAddress[ 4 ] = { configGATEWAY_ADDR0, configGATEWAY_ADDR1, configGATEWAY_ADDR2, configGATEWAY_ADDR3 };
static const uint8_t ucDNSServerAddress[ 4 ] = { configDNS_SERVER_ADDR0, configDNS_SERVER_ADDR1, configDNS_SERVER_ADDR2, configDNS_SERVER_ADDR3 };

/* Set the following constant to pdTRUE to log using the method indicated by the
 * name of the constant, or pdFALSE to not log using the method indicated by the
 * name of the constant.  Options include to standard out (xLogToStdout), to a disk
 * file (xLogToFile), and to a UDP port (xLogToUDP).  If xLogToUDP is set to pdTRUE
 * then UDP messages are sent to the IP address configured as the echo server
 * address (see the configECHO_SERVER_ADDR0 definitions in FreeRTOSConfig.h) and
 * the port number set by configPRINT_PORT in FreeRTOSConfig.h. */
const BaseType_t xLogToStdout = pdTRUE, xLogToFile = pdFALSE, xLogToUDP = pdFALSE;

/* Default MAC address configuration.  The demo creates a virtual network
 * connection that uses this MAC address by accessing the raw Ethernet data
 * to and from a real network connection on the host PC.  See the
 * configNETWORK_INTERFACE_TO_USE definition for information on how to configure
 * the real network connection to use. */
const uint8_t ucMACAddress[ 6 ] = { configMAC_ADDR0, configMAC_ADDR1, configMAC_ADDR2, configMAC_ADDR3, configMAC_ADDR4, configMAC_ADDR5 };

/* Use by the pseudo random number generator. */
static UBaseType_t ulNextRand;

#define USES_IPV6_ENDPOINT    1  /* 0 */

/* A mask of end-points that are up. */
#if ( USES_IPV6_ENDPOINT != 0 )
#define mainNETWORK_UP_COUNT    3U
#else
#define mainNETWORK_UP_COUNT    1U
#endif

static uint32_t uxNetworkisUp = 0U;


/* A semaphore to become idle. */
SemaphoreHandle_t xServerSemaphore;

/*-----------------------------------------------------------*/

BaseType_t xHandleTestingCommand(char* pcCommand,
    size_t uxLength);
void xHandleTesting(void);
void showEndPoint(NetworkEndPoint_t* pxEndPoint);

#if ( ipconfigMULTI_INTERFACE == 1 ) && ( ipconfigCOMPATIBLE_WITH_SINGLE == 0 )
    /* In case multiple interfaces are used, define them statically. */

/* With WinPCap there is only 1 physical interface. */
    static NetworkInterface_t xInterfaces[ 1 ];

/* It will have several end-points. */
    static NetworkEndPoint_t xEndPoints[ 4 ];

/* A function from NetInterface.c to initialise the interface descriptor
 * of type 'NetworkInterface_t'. */
    NetworkInterface_t * pxWinPcap_FillInterfaceDescriptor( BaseType_t xEMACIndex,
                                                           NetworkInterface_t * pxInterface );
#endif /* ipconfigMULTI_INTERFACE */

int main( void )
{
    const uint32_t ulLongTime_ms = pdMS_TO_TICKS( 1000UL );

    /*
     * Instructions for using this project are provided on:
     * http://www.freertos.org/FreeRTOS-Plus/FreeRTOS_Plus_TCP/examples_FreeRTOS_simulator.html
     */

    /* Miscellaneous initialisation including preparing the logging and seeding
     * the random number generator. */
    prvMiscInitialisation();

#if USE_LOG_EVENT
    iEventLogInit();
#endif

    /* Initialise the network interface.
     *
     ***NOTE*** Tasks that use the network are created in the network event hook
     * when the network is connected and ready for use (see the definition of
     * vApplicationIPNetworkEventHook() below).  The address values passed in here
     * are used if ipconfigUSE_DHCP is set to 0, or if ipconfigUSE_DHCP is set to 1
     * but a DHCP server cannot be	contacted. */

    #if ( ipconfigMULTI_INTERFACE == 0 ) || ( ipconfigCOMPATIBLE_WITH_SINGLE == 1 )
        /* Using the old /single /IPv4 library, or using backward compatible mode of the new /multi library. */
        FreeRTOS_debug_printf( ( "FreeRTOS_IPInit\r\n" ) );
        FreeRTOS_IPInit( ucIPAddress, ucNetMask, ucGatewayAddress, ucDNSServerAddress, ucMACAddress );
    #else
        /* Initialise the interface descriptor for WinPCap. */
        pxWinPcap_FillInterfaceDescriptor( 0, &( xInterfaces[ 0 ] ) );
        
        /* === End-point 0 === */
        FreeRTOS_FillEndPoint( &( xInterfaces[ 0 ] ), &( xEndPoints[ 0 ] ), ucIPAddress, ucNetMask, ucGatewayAddress, ucDNSServerAddress, ucMACAddress );
        #if ( ipconfigUSE_DHCP != 0 )
            {
                /* End-point 0 wants to use DHCPv4. */
                xEndPoints[ 0 ].bits.bWantDHCP = pdTRUE;
            }
        #endif /* ( ipconfigUSE_DHCP != 0 ) */
    /* Give an invalid and a valid DNS IP-address. */
/*          xEndPoints[0].ipv4_defaults.ulDNSServerAddresses[0] = FreeRTOS_inet_addr_quick( 118, 98, 44, 10 ); */
/*          xEndPoints[0].ipv4_defaults.ulDNSServerAddresses[1] = FreeRTOS_inet_addr_quick( 118, 98, 44, 100 ); */

        /*
         * End-point-1  // public
         *     Network: 2001:470:ed44::/64
         *     IPv6   : 2001:470:ed44::4514:89d5:4589:8b79/128
         *     Gateway: fe80::ba27:ebff:fe5a:d751  // obtained from Router Advertisement
         */
        #if ( ipconfigUSE_IPv6 != 0 && USES_IPV6_ENDPOINT != 0 )
            {
                IPv6_Address_t xIPAddress;
                IPv6_Address_t xPrefix;
                IPv6_Address_t xGateWay;
        IPv6_Address_t xDNSServer1, xDNSServer2;

        FreeRTOS_inet_pton6("2001:470:ed44::", xPrefix.ucBytes);

        FreeRTOS_CreateIPv6Address(&xIPAddress, &xPrefix, 64, pdTRUE);
        FreeRTOS_inet_pton6("fe80::ba27:ebff:fe5a:d751", xGateWay.ucBytes);

        FreeRTOS_FillEndPoint_IPv6(&(xInterfaces[0]),
            &(xEndPoints[1]),
            &(xIPAddress),
            &(xPrefix),
            64uL, /* Prefix length. */
            &(xGateWay),
            NULL, /* pxDNSServerAddress: Not used yet. */
            ucMACAddress);
        FreeRTOS_inet_pton6("2001:4860:4860::8888", xEndPoints[1].ipv6_settings.xDNSServerAddresses[0].ucBytes);
        FreeRTOS_inet_pton6("fe80::1", xEndPoints[1].ipv6_settings.xDNSServerAddresses[1].ucBytes);
        FreeRTOS_inet_pton6("2001:4860:4860::8888", xEndPoints[1].ipv6_defaults.xDNSServerAddresses[0].ucBytes);
        FreeRTOS_inet_pton6("fe80::1", xEndPoints[1].ipv6_defaults.xDNSServerAddresses[1].ucBytes);

                #if ( ipconfigUSE_RA != 0 )
                    {
                        /* End-point 1 wants to use Router Advertisement / SLAAC. */
            xEndPoints[1].bits.bWantRA = pdTRUE;
                    }
                #endif /* #if( ipconfigUSE_RA != 0 ) */
                #if ( ipconfigUSE_DHCPv6 != 0 )
                    {
                        /* End-point 1 wants to use DHCPv6. */
            xEndPoints[1].bits.bWantDHCP = pdTRUE;
                    }
                #endif /* ( ipconfigUSE_DHCPv6 != 0 ) */
            }
        #endif /* ( ipconfigUSE_IPv6 != 0 ) */
#if ( ipconfigUSE_IPv6 != 0 && USES_IPV6_ENDPOINT != 0 )
    {
        /*
         * End-point-3  // private
         *     Network: fe80::/10 (link-local)
         *     IPv6   : fe80::d80e:95cc:3154:b76a/128
         *     Gateway: -
         */
        {
            IPv6_Address_t xIPAddress;
            IPv6_Address_t xPrefix;

            FreeRTOS_inet_pton6("fe80::", xPrefix.ucBytes);
            FreeRTOS_inet_pton6("fe80::7009", xIPAddress.ucBytes);

            FreeRTOS_FillEndPoint_IPv6(
                &(xInterfaces[0]),
                &(xEndPoints[2]),
                &(xIPAddress),
                &(xPrefix),
                10U,  /* Prefix length. */
                NULL, /* No gateway */
                NULL, /* pxDNSServerAddress: Not used yet. */
                ucMACAddress);
        }
    }
#endif /* if ( ipconfigUSE_IPv6 != 0 ) */
    /* === End-point 0 === */
#if ( ( mainNETWORK_UP_COUNT >= 4U ) || ( USES_IPV6_ENDPOINT == 0 && mainNETWORK_UP_COUNT >= 2U ) )
    {
        /*172.25.201.204 */
        /*netmask 255.255.240.0 */
        const uint8_t ucMACAddress2[6] = { 0x00, 0x22, 0x22, 0x22, 0x22, 82 };
        const uint8_t ucIPAddress2[4] = { 192, 168, 2, 210 };
        const uint8_t ucNetMask2[4] = { 255, 255, 255, 0 };
        const uint8_t ucGatewayAddress2[4] = { 0, 0, 0, 0 };
        FreeRTOS_FillEndPoint(&(xInterfaces[0]), &(xEndPoints[3]), ucIPAddress2, ucNetMask2, ucGatewayAddress2, ucDNSServerAddress, ucMACAddress2);
#if ( ipconfigUSE_DHCP != 0 )
        {
            /* End-point 0 wants to use DHCPv4. */
            xEndPoints[3].bits.bWantDHCP = pdTRUE;
        }
#endif /* ( ipconfigUSE_DHCP != 0 ) */
    }
#endif /* ( mainNETWORK_UP_COUNT >= 3U ) */

        FreeRTOS_IPStart();
    #endif /* if ( ipconfigMULTI_INTERFACE == 0 ) || ( ipconfigCOMPATIBLE_WITH_SINGLE == 1 ) */
    xTaskCreate( prvServerWorkTask, "SvrWork", mainTCP_SERVER_STACK_SIZE, NULL, mainTCP_SERVER_TASK_PRIORITY, NULL );

    /* Start the RTOS scheduler. */
    FreeRTOS_debug_printf( ( "vTaskStartScheduler\r\n" ) );
    vTaskStartScheduler();

    /* If all is well, the scheduler will now be running, and the following
     * line will never be reached.  If the following line does execute, then
     * there was insufficient FreeRTOS heap memory available for the idle and/or
     * timer tasks	to be created.  See the memory management section on the
     * FreeRTOS web site for more details (this is standard text that is not not
     * really applicable to the Win32 simulator port). */
    for( ; ; )
    {
        Sleep( ulLongTime_ms );
    }
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook( void )
{
    const uint32_t ulMSToSleep = 1;

    /* This is just a trivial example of an idle hook.  It is called on each
     * cycle of the idle task if configUSE_IDLE_HOOK is set to 1 in
     * FreeRTOSConfig.h.  It must *NOT* attempt to block.  In this case the
     * idle task just sleeps to lower the CPU usage. */
    Sleep( ulMSToSleep );
}
/*-----------------------------------------------------------*/

void vAssertCalled( const char * pcFile,
                    uint32_t ulLine )
{
    const uint32_t ulLongSleep = 1000UL;
    volatile uint32_t ulBlockVariable = 0UL;
    volatile char * pcFileName = ( volatile char * ) pcFile;
    volatile uint32_t ulLineNumber = ulLine;

    ( void ) pcFileName;
    ( void ) ulLineNumber;

    FreeRTOS_debug_printf( ( "vAssertCalled( %s, %ld\r\n", pcFile, ulLine ) );

    /* Setting ulBlockVariable to a non-zero value in the debugger will allow
     * this function to be exited. */
    taskDISABLE_INTERRUPTS();
    {
        while( ulBlockVariable == 0UL )
        {
            Sleep( ulLongSleep );
        }
    }
    taskENABLE_INTERRUPTS();
}
/*-----------------------------------------------------------*/

/* Called by FreeRTOS+TCP when the network connects or disconnects.  Disconnect
 * events are only received if implemented in the MAC driver. */
/* *INDENT-OFF* */
#if ( ipconfigMULTI_INTERFACE != 0 ) || ( ipconfigCOMPATIBLE_WITH_SINGLE != 0 )
    /* The multi version: each end-point comes up individually. */
    void vApplicationIPNetworkEventHook( eIPCallbackEvent_t eNetworkEvent,
                                         NetworkEndPoint_t * pxEndPoint )
#else
    /* The single version, the interface comes up as a whole. */
    void vApplicationIPNetworkEventHook( eIPCallbackEvent_t eNetworkEvent )
#endif
/* *INDENT-ON* */
{
    static BaseType_t xTasksAlreadyCreated = pdFALSE;

    /* If the network has just come up...*/
    if (eNetworkEvent == eNetworkUp)
    {
        /* Create the tasks that use the IP stack if they have not already been
         * created. */
        uxNetworkisUp++;

        if ((xTasksAlreadyCreated == pdFALSE) && (uxNetworkisUp == mainNETWORK_UP_COUNT))
        {
#if USE_LOG_EVENT
            iEventLogClear();
#endif

            /* See the comments above the definitions of these pre-processor
             * macros at the top of this file for a description of the individual
             * demo tasks. */
            #if ( mainCREATE_SIMPLE_UDP_CLIENT_SERVER_TASKS == 1 )
                {
                    vStartSimpleUDPClientServerTasks( configMINIMAL_STACK_SIZE, mainSIMPLE_UDP_CLIENT_SERVER_PORT, mainSIMPLE_UDP_CLIENT_SERVER_TASK_PRIORITY );
                }
            #endif /* mainCREATE_SIMPLE_UDP_CLIENT_SERVER_TASKS */

            #if ( mainCREATE_TCP_ECHO_TASKS_SINGLE == 1 )
                {
                    vStartTCPEchoClientTasks_SingleTasks( mainECHO_CLIENT_TASK_STACK_SIZE, mainECHO_CLIENT_TASK_PRIORITY );
                }
            #endif /* mainCREATE_TCP_ECHO_TASKS_SINGLE */

            #if ( mainCREATE_TCP_ECHO_SERVER_TASK == 1 )
                {
                    vStartSimpleTCPServerTasks( mainECHO_SERVER_TASK_STACK_SIZE, mainECHO_SERVER_TASK_PRIORITY );
                }
            #endif

            xTasksAlreadyCreated = pdTRUE;
        }

        FreeRTOS_printf(("uxNetworkisUp = %u\n", (unsigned)uxNetworkisUp));

        if (pxEndPoint->bits.bIPv6 == 0U)
        {
            *ipLOCAL_IP_ADDRESS_POINTER = pxEndPoint->ipv4_settings.ulIPAddress;
            FreeRTOS_printf(("IPv4 address = %xip\n", FreeRTOS_ntohl(pxEndPoint->ipv4_settings.ulIPAddress)));
        }

        #if ( ipconfigMULTI_INTERFACE == 0 )
            {
                uint32_t ulIPAddress, ulNetMask, ulGatewayAddress, ulDNSServerAddress;

                /* Print out the network configuration, which may have come from a DHCP
                 * server. */
                FreeRTOS_GetAddressConfiguration( &ulIPAddress, &ulNetMask, &ulGatewayAddress, &ulDNSServerAddress );
                FreeRTOS_inet_ntoa( ulIPAddress, cBuffer );
                FreeRTOS_printf( ( "\r\n\r\nIP Address: %s\r\n", cBuffer ) );

                FreeRTOS_inet_ntoa( ulNetMask, cBuffer );
                FreeRTOS_printf( ( "Subnet Mask: %s\r\n", cBuffer ) );

                FreeRTOS_inet_ntoa( ulGatewayAddress, cBuffer );
                FreeRTOS_printf( ( "Gateway Address: %s\r\n", cBuffer ) );

                FreeRTOS_inet_ntoa( ulDNSServerAddress, cBuffer );
                FreeRTOS_printf( ( "DNS Server Address: %s\r\n\r\n\r\n", cBuffer ) );
            }
        #else /* if ( ipconfigMULTI_INTERFACE == 0 ) */
            {
                /* Print out the network configuration, which may have come from a DHCP
                 * server. */
                showEndPoint( pxEndPoint );
            }
        #endif /* ipconfigMULTI_INTERFACE */
    }
}
/*-----------------------------------------------------------*/

void vApplicationMallocFailedHook( void )
{
    /* Called if a call to pvPortMalloc() fails because there is insufficient
     * free memory available in the FreeRTOS heap.  pvPortMalloc() is called
     * internally by FreeRTOS API functions that create tasks, queues, software
     * timers, and semaphores.  The size of the FreeRTOS heap is set by the
     * configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h. */
    vAssertCalled( __FILE__, __LINE__ );
}
/*-----------------------------------------------------------*/

UBaseType_t uxRand( void )
{
    const uint32_t ulMultiplier = 0x015a4e35UL, ulIncrement = 1UL;

    /* Utility function to generate a pseudo random number. */

    ulNextRand = ( ulMultiplier * ulNextRand ) + ulIncrement;
    return( ( int ) ( ulNextRand >> 16UL ) & 0x7fffUL );
}
/*-----------------------------------------------------------*/

uint32_t uxRand32(void)
{
    /* uxRand only returns 15 random bits. Call it 3 times. */
    uint32_t ul[3] = { uxRand(), uxRand(), uxRand() };
    uint32_t uxReturn = ul[0] | (ul[1] << 15) | (ul[2] << 30);

    return uxReturn;
}

static void prvSRand(UBaseType_t ulSeed)
{
    /* Utility function to seed the pseudo random number generator. */
    ulNextRand = ulSeed;
}
/*-----------------------------------------------------------*/

static void prvMiscInitialisation( void )
{
    time_t xTimeNow;
    uint32_t ulLoggingIPAddress;

    ulLoggingIPAddress = FreeRTOS_inet_addr_quick( configECHO_SERVER_ADDR0, configECHO_SERVER_ADDR1, configECHO_SERVER_ADDR2, configECHO_SERVER_ADDR3 );
    vLoggingInit( xLogToStdout, xLogToFile, xLogToUDP, ulLoggingIPAddress, configPRINT_PORT );

    /* Seed the random number generator. */
    time( &xTimeNow );
    FreeRTOS_debug_printf( ( "Seed for randomiser: %lu\r\n", xTimeNow ) );
    prvSRand( ( uint32_t ) xTimeNow );
    FreeRTOS_debug_printf( ( "Random numbers: %08X %08X %08X %08X\r\n", ipconfigRAND32(), ipconfigRAND32(), ipconfigRAND32(), ipconfigRAND32() ) );
}
/*-----------------------------------------------------------*/

#if ( ipconfigUSE_LLMNR != 0 ) || ( ipconfigUSE_NBNS != 0 ) || ( ipconfigDHCP_REGISTER_HOSTNAME == 1 )

    const char * pcApplicationHostnameHook( void )
    {
        /* Assign the name "FreeRTOS" to this network node.  This function will
         * be called during the DHCP: the machine will be registered with an IP
         * address plus this name. */
        return mainHOST_NAME;
    }

#endif
/*-----------------------------------------------------------*/

#if ( ipconfigUSE_MDNS != 0 ) || ( ipconfigUSE_LLMNR != 0 ) || ( ipconfigUSE_NBNS != 0 )

#if ( ipconfigMULTI_INTERFACE != 0 ) && ( ipconfigUSE_IPv6 != 0 ) && ( TESTING_PATCH == 0 )
static BaseType_t setEndPoint(NetworkEndPoint_t* pxEndPoint)
{
    NetworkEndPoint_t* px;
    BaseType_t xDone = pdFALSE;
    BaseType_t bDNS_IPv6 = (pxEndPoint->usDNSType == dnsTYPE_AAAA_HOST) ? 1 : 0;

    FreeRTOS_printf(("Wanted v%c got v%c\n", bDNS_IPv6 ? '6' : '4', pxEndPoint->bits.bIPv6 ? '6' : '4'));

    if ((pxEndPoint->usDNSType == dnsTYPE_ANY_HOST) ||
        ((pxEndPoint->usDNSType == dnsTYPE_AAAA_HOST) == (pxEndPoint->bits.bIPv6 != 0U)))
    {
        xDone = pdTRUE;
    }
    else
    {
        for (px = FreeRTOS_FirstEndPoint(pxEndPoint->pxNetworkInterface);
            px != NULL;
            px = FreeRTOS_NextEndPoint(pxEndPoint->pxNetworkInterface, px))
        {
            BaseType_t bIPv6 = ENDPOINT_IS_IPv6(px);

            if (bIPv6 == bDNS_IPv6)
            {
                if (bIPv6 != 0)
                {
                    memcpy(pxEndPoint->ipv6_settings.xIPAddress.ucBytes, px->ipv6_settings.xIPAddress.ucBytes, ipSIZE_OF_IPv6_ADDRESS);
                }
                else
                {
                    pxEndPoint->ipv4_settings.ulIPAddress = px->ipv4_settings.ulIPAddress;
                }

                pxEndPoint->bits.bIPv6 = bDNS_IPv6;
                xDone = pdTRUE;
                break;
            }
        }
    }

    if (pxEndPoint->bits.bIPv6 != 0)
    {
        FreeRTOS_printf(("%s address %pip\n", xDone ? "Success" : "Failed", pxEndPoint->ipv6_settings.xIPAddress.ucBytes));
    }
    else
    {
        FreeRTOS_printf(("%s address %xip\n", xDone ? "Success" : "Failed", (unsigned)FreeRTOS_ntohl(pxEndPoint->ipv4_settings.ulIPAddress)));
    }

    return xDone;
}
#endif /* ( ipconfigMULTI_INTERFACE != 0 ) */

/*-----------------------------------------------------------*/

#if ( ipconfigMULTI_INTERFACE != 0 )
BaseType_t xApplicationDNSQueryHook(NetworkEndPoint_t* pxEndPoint,
    const char* pcName)
#else
BaseType_t xApplicationDNSQueryHook(const char* pcName)
#endif
    {
        BaseType_t xReturn;

        /* Determine if a name lookup is for this node.  Two names are given
         * to this node: that returned by pcApplicationHostnameHook() and that set
         * by mainDEVICE_NICK_NAME. */
    const char* serviceName = (strstr(pcName, ".local") != NULL) ? "mDNS" : "LLMNR";

    if (strncasecmp(pcName, "bong", 4) == 0)
        {
#if ( ipconfigUSE_IPv6 != 0 )
        int ip6Preferred = (pcName[4] == '6') ? pdTRUE : pdFALSE;
        /*
        #define dnsTYPE_A_HOST            0x0001U // DNS type A host.
        #define dnsTYPE_AAAA_HOST         0x001CU // DNS type AAAA host.
        */
        xReturn = (pxEndPoint->usDNSType == dnsTYPE_AAAA_HOST) == (ip6Preferred == pdTRUE);
#else
        xReturn = pdTRUE;
#endif
        }
    else if ((strcasecmp(pcName, pcApplicationHostnameHook()) == 0) ||
        (strcasecmp(pcName, "winsim.local") == 0) ||
        (strcasecmp(pcName, "winsim") == 0) ||
        (strcasecmp(pcName, mainDEVICE_NICK_NAME) == 0))
        {
        xReturn = pdTRUE;
        }
        else
        {
            xReturn = pdFAIL;
        }

#if ( ipconfigMULTI_INTERFACE != 0 ) && ( ipconfigUSE_IPv6 != 0 ) && ( TESTING_PATCH == 0 )
    if (xReturn == pdTRUE)
    {
        xReturn = setEndPoint(pxEndPoint);
    }
#endif
    {
#if ( ipconfigMULTI_INTERFACE != 0 ) && ( ipconfigUSE_IPv6 != 0 )
        FreeRTOS_printf(("%s query '%s' = %d IPv%c\n", serviceName, pcName, (int)xReturn, pxEndPoint->bits.bIPv6 ? '6' : '4'));
#else
        FreeRTOS_printf(("%s query '%s' = %d IPv4 only\n", serviceName, pcName, (int)xReturn));
#endif
    }

        return xReturn;
    }
/*-----------------------------------------------------------*/

#endif /* if ( ipconfigUSE_LLMNR != 0 ) || ( ipconfigUSE_NBNS != 0 ) */
/*-----------------------------------------------------------*/

/*
 * Callback that provides the inputs necessary to generate a randomized TCP
 * Initial Sequence Number per RFC 6528.  THIS IS ONLY A DUMMY IMPLEMENTATION
 * THAT RETURNS A PSEUDO RANDOM NUMBER SO IS NOT INTENDED FOR USE IN PRODUCTION
 * SYSTEMS.
 */
extern uint32_t ulApplicationGetNextSequenceNumber( uint32_t ulSourceAddress,
                                                    uint16_t usSourcePort,
                                                    uint32_t ulDestinationAddress,
                                                    uint16_t usDestinationPort )
{
    ( void ) ulSourceAddress;
    ( void ) usSourcePort;
    ( void ) ulDestinationAddress;
    ( void ) usDestinationPort;

    return uxRand32();
}
/*-----------------------------------------------------------*/

/*
 * Supply a random number to FreeRTOS+TCP stack.
 * THIS IS ONLY A DUMMY IMPLEMENTATION THAT RETURNS A PSEUDO RANDOM NUMBER
 * SO IS NOT INTENDED FOR USE IN PRODUCTION SYSTEMS.
 */
BaseType_t xApplicationGetRandomNumber( uint32_t * pulNumber )
{
    *( pulNumber ) = uxRand();
    return pdTRUE;
}
/*-----------------------------------------------------------*/

const char * pcCommandList[] =
{
    /*    "arpqc 2404:6800:4003:c0f::5e",    // a public IP-address */
    /*    "arpqc fe80::ba27:ebff:fe5a:d751", // a gateway */
    /*    "arpqc 192.168.2.1", */
    /*    "arpqc 172.217.194.100", */
    /*    "dnsq4 google.de", */
    /*    "dnsq6 google.nl", */

    /*    "arpqc 192.168.2.1", */
    /*    "arpqc 192.168.2.10", */
    /*    "arpqc 172.217.194.100", */
    /*    "arpqc 2404:6800:4003:c0f::5e", */
        "ifconfig",
        /*      "udp 192.168.2.255@2402 Hello", */
        /*      "udp 192.168.2.255@2402 Hello", */
        /*      "udp 192.168.2.255@2402 Hello", */
        /*      "udp 192.168.2.255@2402 Hello", */
            /*          "http 192.168.2.11 /index.html 33001", */
            /*       "http 2404:6800:4003:c05::5e /index.html 80", */
            /*       "ping6 2606:4700:f1::1", */
            /*       "ping6 2606:4700:f1::1", */
            /*       "dnsq4  google.de", */
            /*       "dnsq6  google.nl", */
            /*       "dnsq4  google.es", */
        /*      "dnsq6  google.co.uk", */
        /*         "udp 192.168.2.11@7 Hello world 1\r\n", */
        /*         "udp fe80::715e:482e:4a3e:d081@7 Hello world 1\r\n", */
        /*      "dnsq4  google.de", */
        /*      "dnsq6  google.nl", */
        /*      "dnsq4  google.es", */
        /*      "dnsq6  google.co.uk", */

        /*       "ntp6a 2.europe.pool.ntp.org", */
        /* //      "ping4c 74.125.24.94", */
        /*       "ping4c 192.168.2.1", */
        /*       "ping4c 192.168.2.10", */
        /*      "ping6c 2404:6800:4003:c11::5e", */
        /*      "ping6c 2404:6800:4003:c11::5e", */

        /*    "ping4 raspberrypi.local", */
        /*    "ping6 2404:6800:4003:c0f::5e", */

        /*    "http4 google.de /index.html", */
        /*    "http6 google.nl /index.html", */
        /*    "ping4 10.0.1.10", */
        /*    "ping4 192.168.2.1", */
        /*    "dnsq4 amazon.com", */
        /*    "ping6 google.de", */
        /*    "ntp6a 2.europe.pool.ntp.org", */
};

static void prvServerWorkTask( void * pvArgument )
{
    BaseType_t xCommandIndex = 0;
    Socket_t xSocket;

    ( void ) pvArgument;
    FreeRTOS_printf( ( "prvServerWorkTask started\n" ) );

    xServerSemaphore = xSemaphoreCreateBinary();
    configASSERT(xServerSemaphore != NULL);

   // pcap_prepare();

    /* Wait for all end-points to come up.
     * They're counted with 'uxNetworkisUp'. */
    do
    {
        vTaskDelay(pdMS_TO_TICKS(100U));
    } while (uxNetworkisUp != mainNETWORK_UP_COUNT);

    xDNS_IP_Preference = xPreferenceIPv6;

    {
        xSocket = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP);
        struct freertos_sockaddr xAddress;

        (void)memset(&(xAddress), 0, sizeof(xAddress));
        xAddress.sin_family = FREERTOS_AF_INET6;
        xAddress.sin_port = FreeRTOS_htons(5000U);

        BaseType_t xReturn = FreeRTOS_bind(xSocket, &xAddress, (socklen_t)sizeof(xAddress));
        FreeRTOS_printf(("Open socket %d bind = %d\n", xSocketValid(xSocket), xReturn));
        TickType_t xTimeoutTime = pdMS_TO_TICKS(10U);
        FreeRTOS_setsockopt(xSocket, 0, FREERTOS_SO_RCVTIMEO, &xTimeoutTime, sizeof(TickType_t));
    }

    for( ; ; )
    {
        char pcCommand[ 129 ];
        TickType_t uxTickCount = pdMS_TO_TICKS( 200U );

        if (xCommandIndex < ARRAY_SIZE(pcCommandList))
        {
            while (uxTickCount != 0)
        {
            xHandleTesting();
                xSemaphoreTake(xServerSemaphore, pdMS_TO_TICKS(10));
            uxTickCount--;
        }

            /*          vTaskDelay( pdMS_TO_TICKS( 1000U ) ); */
            FreeRTOS_printf(("\n"));

            snprintf(pcCommand, sizeof(pcCommand), "%s", pcCommandList[xCommandIndex]);
            FreeRTOS_printf(("\n"));
            FreeRTOS_printf(("/*==================== %s (%d/%d) ====================*/\n",
                pcCommand, xCommandIndex + 1, ARRAY_SIZE(pcCommandList)));
            FreeRTOS_printf(("\n"));
            xHandleTestingCommand(pcCommand, sizeof(pcCommand));
            xCommandIndex++;
        }
        else if (xCommandIndex == ARRAY_SIZE(pcCommandList))
        {
            FreeRTOS_printf(("Server task now ready.\n"));

            
#if ( ipconfigUSE_NTP_DEMO != 0 )
           // if (xNTPTaskIsRunning() != pdFALSE)
        {
                /* Ask once more for the current time. */
             //   vStartNTPTask(0U, 0U);
            }
#endif

            /*vTaskDelete( NULL ); */
            xCommandIndex++;
        }

        {
            char pcBuffer[1500];
            struct freertos_sockaddr xSourceAddress;
            socklen_t xLength = sizeof(socklen_t);
            int32_t rc = FreeRTOS_recvfrom(xSocket, pcBuffer, sizeof(pcBuffer), 0, &xSourceAddress, &xLength);

            if (rc > 0)
            {
                if (xSourceAddress.sin_family == FREERTOS_AF_INET6)
                {
                    FreeRTOS_printf(("Recv UDP %d bytes from %pip port %u\n", rc, xSourceAddress.sin_address.xIP_IPv6.ucBytes, FreeRTOS_ntohs(xSourceAddress.sin_port)));
                }
                else
                {
                    FreeRTOS_printf(("Recv UDP %d bytes from %xip port %u\n", rc, FreeRTOS_ntohl(xSourceAddress.sin_address.ulIP_IPv4), FreeRTOS_ntohs(xSourceAddress.sin_port)));
                }

                if (rc == 14)
                {
                    static BaseType_t xDone = 0;

                    if (xDone == 3)
                    {
                        BaseType_t xIPv6 = (xSourceAddress.sin_family == FREERTOS_AF_INET6) ? pdTRUE : pdFALSE;
                        FreeRTOS_printf(("%d: Clear %s table\n", xDone, xIPv6 ? "ND" : "ARP"));

                        if (xIPv6 == pdTRUE)
                        {
                            FreeRTOS_ClearND();
                        }
                        else
                        {
                            FreeRTOS_ClearARP(NULL);
                        }

                        xDone = 0;
                    }
                    else
                    {
                        xDone++;
                    }
                }
            }
        }
    }
}

#if ( ipconfigUSE_NTP_DEMO != 0 )

/* Some functions to get NTP demo working. */

    extern BaseType_t xNTPHasTime;
    extern uint32_t ulNTPTime;

    struct
    {
        uint32_t ntpTime;
    }
    time_guard;

    int set_time( time_t * pxTime )
    {
        ( void ) pxTime;
        time_guard.ntpTime = ulNTPTime - xTaskGetTickCount() / configTICK_RATE_HZ;
        return 0;
    }
/*-----------------------------------------------------------*/

    time_t get_time( time_t * puxTime )
    {
        time_t xTime = 0U;

        if( xNTPHasTime != pdFALSE )
        {
            TickType_t passed = xTaskGetTickCount() / configTICK_RATE_HZ;
            xTime = ( time_t ) time_guard.ntpTime + ( time_t ) passed;
        }

        if( puxTime != NULL )
        {
            *( puxTime ) = xTime;
        }

        return xTime;
    }
/*-----------------------------------------------------------*/

    struct tm * gmtime_r( const time_t * pxTime,
                          struct tm * tmStruct )
    {
        struct tm tm;

        memcpy( &( tm ), gmtime( pxTime ), sizeof( tm ) );

        if( tmStruct != NULL )
        {
            memcpy( tmStruct, &( tm ), sizeof tm );
        }

        return &( tm );
    }
/*-----------------------------------------------------------*/

#endif /* ( ipconfigUSE_NTP_DEMO != 0 ) */

BaseType_t xApplicationMemoryPermissions( uint32_t aAddress )
{
    (void)aAddress;
    /* Return 1 for readable, 2 for writable, 3 for both. */
    return 0x03;
}
/*-----------------------------------------------------------*/

void vOutputChar(const char cChar,
    const TickType_t xTicksToWait)
{
    (void)cChar;
    (void)xTicksToWait;
}
/*-----------------------------------------------------------*/

#if ( ipconfigSUPPORT_OUTGOING_PINGS == 1 )
/*void vApplicationPingReplyHook(ePingReplyStatus_t eStatus, */
/*    uint16_t usIdentifier) */
/*{ */
/*    ( void ) eStatus; */
/*    FreeRTOS_printf( ( "vApplicationPingReplyHook called for %04x\n", usIdentifier ) ); */
/*} */
#endif

#if ( ipconfigUSE_DHCP_HOOK != 0 )
eDHCPCallbackAnswer_t xApplicationDHCPHook(eDHCPCallbackPhase_t eDHCPPhase,
    uint32_t ulIPAddress)
{
    (void)eDHCPPhase;
    (void)ulIPAddress;
    return eDHCPContinue;
}
#endif

void handle_user_test(char* pcBuffer)
{
}

void show_single_addressinfo(const char* pcFormat,
    const struct freertos_addrinfo* pxAddress)
{
    char cBuffer[40];
    const uint8_t* pucAddress;

#if ( ipconfigUSE_IPv6 != 0 )
    if (pxAddress->ai_family == FREERTOS_AF_INET6)
    {
        struct freertos_sockaddr* sockaddr6 = ((struct freertos_sockaddr*)pxAddress->ai_addr);

        pucAddress = (const uint8_t*)&(sockaddr6->sin_addr6);
    }
    else
#endif /* ( ipconfigUSE_IPv6 != 0 ) */
    {
        pucAddress = (const uint8_t*)&(pxAddress->ai_addr->sin_addr);
    }

    (void)FreeRTOS_inet_ntop(pxAddress->ai_family, (const void*)pucAddress, cBuffer, sizeof(cBuffer));

    if (pcFormat != NULL)
    {
        FreeRTOS_printf((pcFormat, cBuffer));
    }
    else
{
        FreeRTOS_printf(("Address: %s\n", cBuffer));
    }
}
/*-----------------------------------------------------------*/

/**
 * @brief For testing purposes: print a list of DNS replies.
 * @param[in] pxAddress: The first reply received ( or NULL )
 */
void show_addressinfo(const struct freertos_addrinfo* pxAddress)
{
    const struct freertos_addrinfo* ptr = pxAddress;
    BaseType_t xIndex = 0;

    while (ptr != NULL)
    {
        show_single_addressinfo("Found Address: %s\n", ptr);

        ptr = ptr->ai_next;
    }

    /* In case the function 'FreeRTOS_printf()` is not implemented. */
    (void)xIndex;
}
static BaseType_t xDNSResult = -2;
static void vDNSEvent(const char* pcName,
    void* pvSearchID,
    struct freertos_addrinfo* pxAddrInfo)
{
    FreeRTOS_printf(("vDNSEvent: called with %p\n", pxAddrInfo));
    showAddressInfo(pxAddrInfo);

    if (pxAddrInfo != NULL)
    {
        xDNSResult = 0;
    }
}

static void dns_test(const char* pcHostName)
{
    uint32_t ulID = uxRand32();
    BaseType_t rc;
    TickType_t uxTimeout = pdMS_TO_TICKS(2000U);

    FreeRTOS_dnsclear();

    struct freertos_addrinfo xHints;
    struct freertos_addrinfo* pxResult = NULL;

    memset(&xHints, 0, sizeof xHints);
    xHints.ai_family = FREERTOS_AF_INET6;

    rc = FreeRTOS_getaddrinfo(pcHostName, NULL, &xHints, &pxResult);

    FreeRTOS_printf(("Lookup '%s': %d\n", pcHostName, rc));

    FreeRTOS_dnsclear();
    xDNSResult = -2;
    rc = FreeRTOS_getaddrinfo_a(pcHostName,
        NULL,
        &xHints,
        &pxResult, /* An allocated struct, containing the results. */
        vDNSEvent,
        (void*)ulID,
        pdMS_TO_TICKS(1000U));
    vTaskDelay(pdMS_TO_TICKS(1000U));
    rc = xDNSResult;
    FreeRTOS_printf(("Lookup '%s': %d\n", pcHostName, rc));
    /*      FreeRTOS_gethostbyname( pcHostName ); */
}

void showAddressInfo(struct freertos_addrinfo* pxAddrInfo)
{
    if (pxAddrInfo == NULL)
    {
        FreeRTOS_printf(("No DNS results\n"));
    }
    else
    {
        struct freertos_addrinfo* pxIter = pxAddrInfo;

        while (pxIter != NULL)
        {
            if (pxIter->ai_family == FREERTOS_AF_INET6)
            {
                FreeRTOS_printf(("DNS result '%s': %pip\n", pxIter->ai_canonname, pxIter->ai_addr->sin_address.xIP_IPv6.ucBytes));
            }
            else
            {
                FreeRTOS_printf(("DNS result '%s': %xip\n", pxIter->ai_canonname, pxIter->ai_addr->sin_address.ulIP_IPv4));
            }

            pxIter = pxIter->ai_next;
        }
    }
}
