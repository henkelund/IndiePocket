/* Copyright (C) 2016 Henrik Hedelund.

   This file is part of IndiePocket.

   IndiePocket is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   IndiePocket is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with IndiePocket.  If not, see <http://www.gnu.org/licenses/>. */

#ifndef PCKT_KIT_FACTORY_H
#define PCKT_KIT_FACTORY_H 1

#include "pckt.h"
#include "kit.h"

__BEGIN_DECLS

typedef struct PcktKitFactoryImpl PcktKitFactory;

typedef struct _PcktKitParserIface PcktKitParserIface;
struct _PcktKitParserIface {
  PcktKit * (*load) (PcktKitParserIface *, const PcktKitFactory *);
  void (*free) (PcktKitParserIface *, const PcktKitFactory *);
};
typedef PcktKitParserIface * (*PcktKitParserCtor) (const PcktKitFactory *);

extern PcktKitFactory *pckt_kit_factory_new (const char *, PcktStatus *);
extern void pckt_kit_factory_free (PcktKitFactory *);
extern PcktKit *pckt_kit_factory_load (const PcktKitFactory *);

extern const char *pckt_kit_factory_get_filename (const PcktKitFactory *);
extern const char *pckt_kit_factory_get_basedir (const PcktKitFactory *);
extern char *pckt_kit_factory_get_abspath (const PcktKitFactory *, const char *);

__END_DECLS

#endif /* ! PCKT_KIT_FACTORY_H */
