#pragma once

#if defined(SUPACHAT_DEVICE_JUJU)
#include "splash_logo_juju.h"
#elif defined(SUPACHAT_DEVICE_PAPA)
#include "splash_logo_papa.h"
#elif defined(SUPACHAT_DEVICE_EMMANUELLE)
#include "splash_logo_emmanuelle.h"
#elif defined(SUPACHAT_DEVICE_NAOMIE)
#include "splash_logo_naomie.h"
#else
#include "splash_logo_albie.h"
#endif
