/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (c) 2024 Meindert Kempe
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef WEB_INTERFACE_H
#define WEB_INTERFACE_H
#pragma once

#include "esp_http_server.h"

esp_err_t wi_start(httpd_handle_t *handle);

void wi_stop(httpd_handle_t handle);

#endif /* WEB_INTERFACE_H */
