#ifndef GRAPHLAB_METRICS_SERVER_HPP
#define GRAPHLAB_METRICS_SERVER_HPP
#include <string>
#include <map>
#include <utility>
#include <boost/function.hpp>


namespace graphlab {
typedef boost::function<std::pair<std::string, std::string>
                                (std::map<std::string, std::string>&)> 
        http_redirect_callback_type;




/**
  \brief This is used to map a URL on the mtrics server 
             to a processing function.
  
  The processing function must have the prototype
  \code
  std::pair<std::string, std::string> callback(std::map<std::string, std::string>&)
  \endcode

  The processing function takes a map of GET variables to their corresponding
  values, and returns a pair of strings. (content_type, content)
  \li \c content type is the http content_type header. For instance text/plain
     or text/html.
  \li \c content is the actual body

  For instance: The builtin 404 handler looks like this:

  \code
  std::pair<std::string, std::string> 
  four_oh_four(std::map<std::string, std::string>& varmap) {
    return std::make_pair(std::string("text/html"), 
                        std::string("Page Not Found"));
  }
  \endcode

  All callbacks should be registered prior to launching the metric server.

  \param page The page to map. For instance <code>page = "a.html"</code>
              will be shown on http://[server]/a.html
  \param callback The callback function to use to process the page
 */
void add_metric_server_callback(std::string page, 
                                http_redirect_callback_type callback);


/**
  \brief Starts the metrics reporting server.


  The function may be called by all machines simultaneously since it only
  does useful work on machine 0.
 */
void launch_metric_server();



/**
  \brief Stops the metrics reporting server if one is started.

  The function may be called by all machines simultaneously since it only
  does useful work on machine 0.
 */
void stop_metric_server();

/**
  \brief Waits for a ctrl-D on machine 0, and 
         stops the metrics reporting server if one is started.

  The function may be called by all machines simultaneously since it only
  does useful work on machine 0. It waits for the stdin stream to close
  (when the user hit ctrl-d), then shuts down the server.
*/
void stop_metric_server_on_eof();

} // graphlab 
#endif // GRAPHLAB_METRICS_SERVER_HPP
