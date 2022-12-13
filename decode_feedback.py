#!/usr/bin/env python

"""
This is a script for decoding feedback read from the Hatch Rest, following the example
of https://github.com/dgreif/homebridge-hatch-baby-rest/blob/7cdbd5909fc798d558f197b6150b5e3a70bdae48/packages/homebridge-hatch-rest-bluetooth/feedback.ts.

Feedback can be read from characteristic "02260002-5efd-47eb-9c1a-de53f7a2b232",
under service "02260001-5efd-47eb-9c1a-de53f7a2b232".

An example feedback data string is "5463986472430000007F53056550C26500000000"
(Rest was off in that example). Another example from when the Rest was on is
"546398728F430000007F53056550026500000000".
"""

import sys

FEEDBACK_FIELDS = {
  "54": { # time
    "length": 4,
  },
  "43": { # color
    "length": 4,
    # TODO: add decoder
  },
  "50": { # powerPreset
    "length": 1,
    # TODO: add decoder
  },
  "53": { # audio
    "length": 2,
    # TODO: add decoder
  },
}

def decode_feedback(feedback):
  # Hmm, I'm not sure, there seems to be some gibberish at the end.
  #if len(feedback) != 40:
  #  raise ValueError("Expected feedback to be 20 bytes (40 hex charcters)")

  index = 0
  expected_fields = set(FEEDBACK_FIELDS.keys())
  found_fields = set()

  while index < len(feedback):
    field_key = feedback[index:index+2]
    found_fields.add(field_key)
    field = FEEDBACK_FIELDS[field_key]

    field_length = field["length"]
    next_field_start_index = index + 2 * (1 + field_length)
    field_data = feedback[index+2:next_field_start_index]

    def int_decoder(data):
      return int(data, 16)

    decoder = FEEDBACK_FIELDS.get("mapper", int_decoder)
    decoded_data = decoder(field_data)

    # TODO: delete
    print(f"Field {field_key} has length {field_length} with data {field_data} (decoded: {decoded_data})")

    index = next_field_start_index

    if found_fields == expected_fields:
      break


if __name__ == "__main__":
  try:
    feedback = sys.argv[1]
    decode_feedback(feedback)
  except IndexError as _:
    print('Must provide feedback as argument (in hex format)!')
    sys.exit(1)
