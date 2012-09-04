////////////////////////////////////////////////////////////////////////////////
/// @file         CRtdsAdapter.cpp
///
/// @author       Yaxi Liu <ylztf@mst.edu>
/// @author       Thomas Roth <tprfh7@mst.edu>
/// @author       Mark Stanovich <stanovic@cs.fsu.edu>
/// @author       Michael Catanzaro <michael.catanzaro@mst.edu>
///
/// @project      FREEDM DGI
///
/// @description  DGI side implementation of the communication protocol to RTDS
///               simulation
///
/// @functions    EndianSwapIfNeeded
///               CRtdsAdapter::Create
///               CRtdsAdapter::~CRtdsAdapter
///               CRtdsAdapter::Run
///               CRtdsAdapter::Start
///               CRtdsAdapter::CRtdsAdapter
///               CRtdsAdapter::Quit
///
/// These source code files were created at Missouri University of Science and
/// Technology, and are intended for use in teaching or research. They may be
/// freely copied, modified, and redistributed as long as modified versions are
/// clearly marked as such and this notice is not removed. Neither the authors
/// nor Missouri S&T make any warranty, express or implied, nor assume any legal
/// responsibility for the accuracy, completeness, or usefulness of these files
/// or any information distributed with these files.
///
/// Suggested modifications or questions about these files can be directed to
/// Dr. Bruce McMillin, Department of Computer Science, Missouri University of
/// Science and Technology, Rolla, MO 65409 <ff@mst.edu>.
////////////////////////////////////////////////////////////////////////////////

#include "CRtdsAdapter.hpp"
#include "CLogger.hpp"

#include <sys/param.h>

#include <boost/asio.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/static_assert.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/property_tree/ptree.hpp>

namespace freedm {
namespace broker {
namespace device {

// FPGA is expecting 4-byte floats.
BOOST_STATIC_ASSERT(sizeof(SignalValue) == 4);

namespace {

/// This file's logger.
CLocalLogger Logger(__FILE__);

///////////////////////////////////////////////////////////////////////////////
/// Converts the SignalValues in the passed vector from big-endian to
/// little-endian, or vice-versa, iff the DGI is running on a little-endian
/// system.
///
/// @pre none
/// @post the elements of data are converted in endianness if the DGI is
///  running on a little-endian system. Otherwise, nothing happens.
/// @param v the vector of SignalValues to be endian-swapped
///////////////////////////////////////////////////////////////////////////////
inline void EndianSwapIfNeeded(std::vector<SignalValue> v)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
// check endianess at compile time.  Middle-Endian not allowed
// The parameters __BYTE_ORDER, __LITTLE_ENDIAN, __BIG_ENDIAN should
// automatically be defined and determined in sys/param.h, which exists
// in most Unix systems.
#if __BYTE_ORDER == __LITTLE_ENDIAN
    for( size_t i = 0; i < v.size(); i += sizeof(SignalValue) )
    {
        SignalValue* tmp = new SignalValue[sizeof(SignalValue)];

        for( unsigned int j = 0; j < sizeof(SignalValue); j++ )
        {
            tmp[j] = v[sizeof(SignalValue) - 1 - j];
        }

        for( unsigned int j = 0; j < sizeof(SignalValue); j++ )
        {
            v[j] = tmp[j];
        }

        delete [] tmp;
    }
#elif __BYTE_ORDER == __BIG_ENDIAN
    Logger.Debug << "Endian swap skipped: host is big-endian." << std::endl;
#else
#error "unsupported endianness or __BYTE_ORDER not defined"
#endif
}

} //unnamed namespace

///////////////////////////////////////////////////////////////////////////////
/// CRtdsAdapter::Create
///
/// @description Creates a RTDS client on the given io_service. This is the 
///              connection point with FPGA.
///
/// @Shared_Memory Uses the passed io_service
///
/// @pre service has not been connected and the ptree conforms to the documented
///      requirements (see FREEDM Wiki)
/// @post CRtdsAdapter object is returned for use
///
/// @param service the io_service to be used to communicate with the FPGA
/// @param ptree the XML property tree specifying this adapter's state and
///              command tables
///
/// @return shared pointer to the new CRtdsAdapter object
///
/// @limitations client must call Connect before the CRtdsAdapter can be used
///////////////////////////////////////////////////////////////////////////////
IAdapter::Pointer CRtdsAdapter::Create(boost::asio::io_service & service,
        const boost::property_tree::ptree & ptree)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    return CRtdsAdapter::Pointer(new CRtdsAdapter(service, ptree));
}

////////////////////////////////////////////////////////////////////////////////
/// CRtdsAdapter::CRtdsAdapter
///
/// @description Constructs a RTDS client
///
/// @Shared_Memory Uses the passed io_service
///
/// @pre service has not been connected and the ptree conforms to the documented
///      requirements (see FREEDM Wiki)
/// @post CRtdsAdapter created
///
/// @param service the io_service to be used to communicate with the FPGA
/// @param ptree the XML property tree specifying this adapter's state and
///              command tables
///
/// @limitations client must call Connect before the CRtdsAdapter can be used
////////////////////////////////////////////////////////////////////////////////
CRtdsAdapter::CRtdsAdapter(boost::asio::io_service & service,
                           const boost::property_tree::ptree & ptree)
  : ITcpAdapter(service, ptree), m_GlobalTimer(service)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
}

////////////////////////////////////////////////////////////////////////////////
/// CRtdsAdapter::Run
///
/// @description
///     This is the main communication engine.
///
/// @I/O
///     At every timestep, a message is sent to the FPGA via TCP socket
///     connection, then a message is retrieved from FPGA via the same
///     connection. On the FPGA side, it's the reverse order -- receive and then
///     send. Both DGI and FPGA sides' receive functions will block until a
///     message arrives, creating a synchronous, lock-step communication between
///     DGI and the FPGA. We keep the timestep (a static member of CRtdsAdapter)
///     very small so that how frequently send and receive get executed is
///     dependent on how fast the FPGA runs.
///
/// @Error_Handling
///     Throws an exception if reading from or writing to the socket fails
///
/// @pre
///     Connection with FPGA is established.
///
/// @post
///     All values in the cmdTable is written to a buffer and sent to FPGA.
///     All values in the stateTable is rewritten with value received
///     from FPGA.
///
/// @limitations
///     Synchronous commnunication
////////////////////////////////////////////////////////////////////////////////
void CRtdsAdapter::Run()
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;

    

    // Always send data to FPGA first
    if( !m_txBuffer.empty() )
    {
        boost::unique_lock<boost::shared_mutex> writeLock(m_txMutex);
        Logger.Debug << "Obtained txBuffer mutex" << std::endl;
        EndianSwapIfNeeded(m_txBuffer);
        try
        {
            boost::asio::write(m_socket,
                    boost::asio::buffer(m_txBuffer, 
                            m_txBuffer.size() * sizeof(SignalValue)));
        }
        catch (std::exception& e)
        {
            throw std::runtime_error("Send to FPGA failed: "
                                     + std::string(e.what()));
        }
        EndianSwapIfNeeded(m_txBuffer);
        Logger.Debug << "Releasing txBuffer mutex" << std::endl;
    }

    // Receive data from FPGA next
    if( !m_rxBuffer.empty() )
    {
        // must be a unique_lock for endian swaps
        boost::unique_lock<boost::shared_mutex> writeLock(m_rxMutex);
        Logger.Debug << "Obtained rxBuffer mutex" << std::endl;
        EndianSwapIfNeeded(m_rxBuffer);
        try
        {
            boost::asio::read(m_socket,
                    boost::asio::buffer(m_rxBuffer,
                            m_rxBuffer.size() * sizeof(SignalValue)));
        }
        catch (std::exception & e)
        {
            throw std::runtime_error("Receive from FPGA failed: " 
                                     + std::string(e.what()));
        }
        EndianSwapIfNeeded(m_rxBuffer);
        Logger.Debug << "Releasing rxBuffer mutex" << std::endl;
    }

    //Start the timer; on timeout, this function is called again
    m_GlobalTimer.expires_from_now(boost::posix_time::microseconds(TIMESTEP));
    m_GlobalTimer.async_wait(boost::bind(&CRtdsAdapter::Run, this));
}

////////////////////////////////////////////////////////////////////////////
/// Quit
///
/// @description
///     closes the socket.
///
/// @pre
///     The socket connection has been established
///
/// @post
///     Closes m_socket
///
/// @limitations
///     None
///
////////////////////////////////////////////////////////////////////////////
void CRtdsAdapter::Quit()
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    m_socket.close();
}

////////////////////////////////////////////////////////////////////////////
/// ~CRtdsAdapter
///
/// @description
///     Closes the socket before destroying an object instance.
///
/// @pre
///     none
///
/// @post
///     m_socket is closed
///
/// @limitations
///     none
///
////////////////////////////////////////////////////////////////////////////
CRtdsAdapter::~CRtdsAdapter()
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    
    if (m_socket.is_open())
    {
        Quit();
    }

}

}//namespace broker
}//namespace freedm
}//namespace device
