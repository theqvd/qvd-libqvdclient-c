/*
 * libqvdclient qvdvm.h
 *
 * Copyright (C) 2016  theqvd.com trade mark of Qindel Formacion y Servicios SL
 *
 * libqvdclient is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _QVDVM_H
#define _QVDVM_H
#include "qvdclient.h"

vm *QvdVmNew(int id, const char *name, const char *state, int blocked);
void QvdVmFree(vm *ptr);
void QvdVmListInit(vmlist *ptr);
void QvdVmListAppendVm(qvdclient *qvd, vmlist *vmlistptr, vm *vmptr);
void QvdVmListFree(vmlist *vmlistptr);

#endif
