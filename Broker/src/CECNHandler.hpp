////////////////////////////////////////////////////////////////////////////////
/// @file         CECNHandler.cpp
///
/// @author       Derek Ditch <derek.ditch@mst.edu>
/// @author       Stephen Jackson <scj7t4@mst.edu>
///
/// @project      FREEDM DGI
///
/// @description  Handles ECN messages from network devices.
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

#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>

#include "CGlobalConfiguration.hpp"

namespace freedm {
namespace broker {

class CECNHandler
{
public:
	static CECNHandler& Instance();

    /// Bind the listener to the specified endpoint and listen for datagrams.
    void Start(boost::asio::ip::udp::endpoint& endpoint);

    /// Stop any current async read and close the socket
    void Stop();

    /// Gets the listener socket
    boost::asio::ip::udp::socket& GetSocket() { return m_socket; };

    // Schedules listening for the next message
    void ScheduleListen();

private:
    /// Private constructor for the singleton instance
    CECNHandler();

    /// Handle completion of a read operation.
    void HandleRead(const boost::system::error_code& e, std::size_t bytes_transferred);

    /// Buffer for incoming data.
    boost::array<char, CGlobalConfiguration::MAX_PACKET_SIZE> m_buffer;

    /// Socket for the CConnection.
    boost::asio::ip::udp::socket m_socket;

    /// Endpoint for incoming message
    boost::asio::ip::udp::endpoint m_recv_from;
};

}
}
