#include <algorithm>
#include <stdexcept>
#include "keyboard_events_extractor.hh"

static
void assert_song_valid(std::vector<key_event>& key_events)
{
  // precond the events MUST be sorted by time. this function only works on that case
  if (not std::is_sorted(key_events.begin(), key_events.end(), [] (const key_event& a, const key_event& b) {
	return a.time < b.time;
      }))
  {
    throw std::invalid_argument("Error, events are not sorted by play time");
  }

  // all keys starts up.
  std::vector<bool> is_pressed (std::numeric_limits<uint8_t>::max(), false);

  for (const auto& event : key_events)
  {
    const auto pitch = event.data.pitch;
    if (event.data.ev_type == key_data::type::pressed)
    {
      if (is_pressed[pitch])
      {
	throw std::invalid_argument("Error: pressing a key that is already pressed");
      }
      is_pressed[pitch] = true;
    }

    if (event.data.ev_type == key_data::type::released)
    {
      if (not (is_pressed[pitch]))
      {
	throw std::invalid_argument("Error: releasing a key that is not pressed");
      }
      is_pressed[pitch] = false;
    }

  }
}

static
void separate_release_pressed_events(std::vector<key_event>& key_events)
{
  // sanity check:
  assert_song_valid(key_events);

  // for each pressed event, look if there is another pressed event that happens
  // at the exact same time as its associated release event. If so, shorten the
  // duration of the former pressed event (i.e advance the time the release
  // event occurs).

  // suboptimal implementation in the case the key_events are sorted
  for (auto& k : key_events)
  {
    if (k.data.ev_type == key_data::type::pressed)
    {
      const auto pitch = k.data.pitch;
      const auto earliest_time = k.time;

      // is there a realease happening at the same time?
      auto release_pos = std::find_if(key_events.begin(), key_events.end(), [=] (const key_event& elt) {
	  return (elt.time == earliest_time) and (elt.data.ev_type == key_data::type::released) and (elt.data.pitch == pitch);
	});

      if (release_pos != key_events.end())
      {

	// there _is_ a release key happening at the same time.
	// Let's find the pressed key responsible for it
	const auto note_start_pos = std::find_if(key_events.rbegin(), key_events.rend(), [=] (const key_event& elt) {
	  return (elt.time < earliest_time) and (elt.data.ev_type == key_data::type::pressed) and (elt.data.pitch == pitch);
	});

	// sanity check: a release event must be preceded by a pressed event.
	if (note_start_pos == key_events.rend())
	{
	  throw std::invalid_argument("error, a there is release event comming from nowhere (failed to find the associated pressed event)");
	}

	// compute the shortening time
	const auto duration = release_pos->time - note_start_pos->time;
	const auto max_shortening_time = decltype(duration){75000000}; // nanoseconds

	// shorten the duration by one fourth of its time, in the worst case
	const auto shortening_time = std::min(max_shortening_time, duration / 4);
	release_pos->time -= shortening_time;

      }
    }
  }

  // sanity check: a key release and a key pressed event with the same pitch
  // can't appear at the same time any more
  for (const auto& k : key_events)
  {
    if (k.data.ev_type == key_data::type::released)
    {
      const auto pitch = k.data.pitch;
      const auto time = k.time;

      if (std::any_of(key_events.begin(), key_events.end(), [=] (const struct key_event& a) {
  	    return (a.data.ev_type == key_data::type::pressed) and (a.data.pitch == pitch) and (a.time == time);
  	  }))
      {
  	throw std::invalid_argument("Error: a key is said to be pressed and released at the same time");
      }
    }
  }

  // sort the keys by time. In some rare cases, when separating a release and a
  // pressed event by making the release happen a bit before, it is possible
  // that the new shorter time, is lower than the time of the event that was
  // happening just before. Therefore, these two events must be reordered
  // appropriately.

  std::stable_sort(key_events.begin(), key_events.end(), [] (const auto& a, const auto&b) {
      return a.time < b.time;
    });

  // post condition: the song must be human playable by now
  assert_song_valid(key_events);
}



std::vector<key_event> get_key_events(const std::vector<note_t>& notes)
{
  std::vector<key_event> res;
  res.reserve(notes.size() * 2); // two events per note, the key down
				 // and key up event

  for (const auto& note : notes)
  {
    if (note.is_played)
    {
      res.emplace_back(key_event{ .time = note.start_time,
				  .data = { .pitch = note.pitch,
					    .ev_type = key_data::type::pressed,
					    .staff_number = note.staff_number }});
      res.emplace_back(key_event{ .time = note.stop_time,
				  .data = { .pitch = note.pitch,
					    .ev_type = key_data::type::released,
					    .staff_number = note.staff_number }});
    }
  }
  res.shrink_to_fit();

  std::stable_sort(res.begin(), res.end(), [] (const auto& a, const auto&b) {
      return a.time < b.time;
    });

  separate_release_pressed_events(res);
  return res;
}
