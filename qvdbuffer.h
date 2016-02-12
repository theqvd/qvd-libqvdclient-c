/*
 * libqvdclient qvdbuffer.h
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
 */
#include <stdlib.h>
#ifndef QVDBUFFER_H
#define QVDBUFFER_H

typedef struct {
    char data[BUFFER_SIZE];
    int offset;
    int size;
} QvdBuffer;

void QvdBufferInit(QvdBuffer *self);
int QvdBufferCanRead(QvdBuffer *self);
int QvdBufferCanWrite(QvdBuffer *self);
void QvdBufferReset(QvdBuffer *self);
int QvdBufferAppend(QvdBuffer *self, const char *data, size_t count);
int QvdBufferRead(QvdBuffer *self, int fd);
int QvdBufferWrite(QvdBuffer *self, int fd);

#endif
