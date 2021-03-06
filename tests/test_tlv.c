/*
 * koruza-driver - KORUZA driver
 *
 * Copyright (C) 2016 Jernej Kos <jernej@kos.mx>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "message.h"

#include <stdio.h>

int main()
{
  message_t msg;
  message_init(&msg);
  message_tlv_add_command(&msg, COMMAND_RESTORE_MOTOR);
  tlv_motor_position_t position = {-18004, -18009, 0};
  message_tlv_add_motor_position(&msg, &position);
  message_tlv_add_checksum(&msg);

  printf("Generated protocol message: ");
  message_print(&msg);
  printf("\n");

  uint8_t buffer[1024];
  size_t length = message_serialize(buffer, 1024, &msg);
  printf("Serialized protocol message:\n");
  for (size_t i = 0; i < length; i++) {
    printf("%02X ", buffer[i]);
  }
  printf("\n");

  message_t msg_parsed;
  message_result_t result = message_parse(&msg_parsed, buffer, length);
  if (result == MESSAGE_SUCCESS) {
    printf("Parsed protocol message: ");
    message_print(&msg_parsed);
    printf("\n");
  } else {
    printf("Failed to parse serialized message: %d\n", result);
    message_free(&msg);
    return -1;
  }

  tlv_command_t parsed_command;
  tlv_motor_position_t parsed_position;
  if (message_tlv_get_command(&msg, &parsed_command) != MESSAGE_SUCCESS) {
    printf("Failed to get command TLV.\n");
    message_free(&msg);
    return -1;
  }

  if (message_tlv_get_motor_position(&msg, &parsed_position) != MESSAGE_SUCCESS) {
    printf("Failed to get motor position TLV.\n");
    message_free(&msg);
    return -1;
  }

  printf("Parsed command %u and motor position (%d, %d, %d)\n",
    parsed_command,
    parsed_position.x, parsed_position.y, parsed_position.z
  );

  if (parsed_command != COMMAND_RESTORE_MOTOR ||
      parsed_position.x != position.x ||
      parsed_position.y != position.y ||
      parsed_position.z != position.z) {
    printf("Parsed values are invalid.\n");
    message_free(&msg);
    return -1;
  }

  message_free(&msg);

  return 0;
}
