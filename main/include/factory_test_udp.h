#ifndef FACTORY_TEST_UDP_H
#define FACTORY_TEST_UDP_H

#include "esp_err.h"

/** Start the production-test UDP listener after platform initialization. */
esp_err_t FactoryTestUdpStart(void);

/** Request the production-test UDP listener to stop and release its socket. */
void FactoryTestUdpStop(void);

#endif /* FACTORY_TEST_UDP_H */
