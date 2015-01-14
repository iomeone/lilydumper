#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <algorithm>
#include "svg_extractor.hh"

static
inline bool is_digit(char c)
{
  return (c >= '0') and (c <= '9');
}

static inline
bool begins_by(const char* s1, const char* s2)
{
  return std::strncmp(s1, s2, std::strlen(s2)) == 0;
}

// str must be of format "-?[[:digit:]]+\.[[:digit:]]{4}"
static bool is_valid_number(const char* str)
{
  if (str == nullptr)
  {
    return false;
  }

  unsigned int pos = 0;
  if (str[0] == '-')
  {
    pos++;
  }

  do
  {
    // must start with at least one digit
    if (not is_digit(str[pos]))
    {
      return false;
    }

    pos++;
  } while (str[pos] != '.');

  pos++;

  // must have 4 digits, and then finish
  for (unsigned int i = 0; i < 4; i++)
  {
    // must start with at least one digit
    if (not is_digit(str[pos]))
    {
      return false;
    }

    pos++;
  }

  // must be finished now
  return (str[pos] == '\0');
}

template <typename T>
static T to_int_decimal_shift(const char* str)
{
  // sanity check:
  if (not is_valid_number(str))
  {
    throw std::runtime_error("Error: invalid param in function to_int_decimal_shift");
  }

  const bool is_neg = (str[0] == '-');
  const uint8_t pos = (is_neg ? 1 : 0);
  char* point_pos;
  const T int_part = static_cast<T>( std::strtoul(&(str[pos]), &point_pos, 10) );
  const T dec_part = static_cast<T>( std::strtoul(&(point_pos[1]), nullptr, 10) );

  const T num = 10000 * int_part + dec_part;
  return is_neg ? -num : num;
}

struct line
{
    uint32_t x1;
    uint32_t y1;
    uint32_t x2;
    uint32_t y2;
};

static
line get_line(const pugi::xml_node& node)
{
  const std::string transform { node.attribute("transform").value() };
  const auto x1 = node.attribute("x1").value();
  const auto x2 = node.attribute("x2").value();
  const auto y1 = node.attribute("y1").value();
  const auto y2 = node.attribute("y2").value();

  const auto translate_str = "translate(";
  if (not begins_by(transform.c_str(), translate_str))
  {
    throw std::runtime_error("Error: lines must have a translate transformation");
  }

  // x_tr and y_tr are initialised with the part inside translate=(...)
  // e.g. if transform == "translate(14.2264, 33.0230)"
  // x_tr will be "14.2264" and y_tr "33.0230"
  const auto separation_pos = transform.find(", ");
  if (separation_pos == std::string::npos)
  {
    throw std::runtime_error("Error: coordinates in translate must be separated by ', '");
  }

  const std::string x_tr (std::begin(transform) + static_cast<int>(std::strlen(translate_str)),
			  std::begin(transform) + static_cast<int>(separation_pos));
  const std::string y_tr (std::begin(transform) + static_cast<int>(separation_pos + 2) /* 2 for COMMA SPACE */,
			  std::end(transform) - 1 /* -1 to remove the ')' */);

  // one could see a potential problem in case of invalid in translate
  // e.g. with "translate(14.2264, ", or "translate(14.2264, 33.0230[dd" y_tr
  // would have a wrong value. But this will be checked by the
  // to_int_decimal_shift function
  const auto x_tr_value = to_int_decimal_shift<decltype(line::x1)>(x_tr.c_str());
  const auto y_tr_value = to_int_decimal_shift<decltype(line::y1)>(y_tr.c_str());

  return line{
    .x1 = (to_int_decimal_shift<decltype(line::x1)>(x1) + x_tr_value),
      .y1 = (to_int_decimal_shift<decltype(line::y1)>(y1) + y_tr_value),
      .x2 = (to_int_decimal_shift<decltype(line::x2)>(x2) + x_tr_value),
      .y2 = (to_int_decimal_shift<decltype(line::y2)>(y2) + y_tr_value) };
}

static
std::vector<line> get_lines_by_xpath(const pugi::xml_document& svg_file,
				     const char* xpath_query)
{
  std::vector<line> res;

  for (const auto& xpath_node : svg_file.select_nodes(xpath_query))
  {
    res.emplace_back( get_line(xpath_node.node()) );
  }
  return res;
}


struct rect
{
    uint32_t top;
    uint32_t bottom;
    uint32_t left;
    uint32_t right;
};

static
std::vector<rect> get_staves_surface(const pugi::xml_document& svg_file)
{
  std::vector<rect> staves;

  // staves are composed by 5 equaly distanced lines,
  // these lines are not part of a <g color=...>...</g> node
  // Xpath -> '//*[not(self::g)]/line'
  // also, since they must be horizontal, one can select restrain to
  // nodes with attribute [@y1 == @y2]
  std::vector<line> lines = get_lines_by_xpath(svg_file, "//*[not(self::g)]/line");

  // sanity check: lines should all be horizontal => y1 == y2
  if (std::any_of(lines.begin(), lines.end(), [] (const auto& a) {
	return a.y1 != a.y2;
      }))
  {
    throw std::runtime_error("Error: a line was expected to be horizontal");
  }

  // sort from top to bottom
  std::sort(lines.begin(), lines.end(), [] (const auto& a, const auto& b) {
      return (a.y1 < b.y1) or ((a.y1 == b.y1) and (a.x1 < b.x1));
    });

  unsigned int i = 0;
  const auto nb_elt = lines.size();
  while (i + 4 < nb_elt)
  {
    const auto dist_01 = (lines[i + 1].y1 - lines[  i  ].y1);
    const auto dist_12 = (lines[i + 2].y1 - lines[i + 1].y1);
    const auto dist_23 = (lines[i + 3].y1 - lines[i + 2].y1);
    const auto dist_34 = (lines[i + 4].y1 - lines[i + 3].y1);

    if ((lines[i].x1     == lines[i + 1].x1) and
	(lines[i].x2     == lines[i + 1].x2) and

	(lines[i + 1].x1 == lines[i + 2].x1) and
	(lines[i + 1].x2 == lines[i + 2].x2) and

	(lines[i + 2].x1 == lines[i + 3].x1) and
	(lines[i + 2].x2 == lines[i + 3].x2) and

	(lines[i + 3].x1 == lines[i + 4].x1) and
	(lines[i + 3].x2 == lines[i + 4].x2) and

	(dist_01 == dist_12) and
	(dist_12 == dist_23) and
	(dist_23 == dist_34) and
	(dist_01 != 0))
    {
      staves.emplace_back(rect{
	  .top    = lines[i].y1,
	  .bottom = lines[i + 4].y1,
	  .left   = lines[i + 4].x1,
	  .right  = lines[i + 4].x2 });

      i += 5;
    }
    else
    {
      i++;
    }
  }

  return staves;
}


struct skyline
{
    rect surface;
    std::vector<h_segment> full_line;
};


static
std::vector<skyline> get_skylines(const pugi::xml_document& svg_file,
				  const char* xpath_query)
{
  std::vector<skyline> res;
  for (const auto& xpath_node : svg_file.select_nodes(xpath_query))
  {
    std::vector<h_segment> full_line;
    for (const auto& line_node : xpath_node.node().select_nodes("./line[(@y1 = @y2) and (@x1 != @x2)]"))
    {
      const auto this_line = get_line(line_node.node());
      full_line.emplace_back(h_segment{
	  .x1 = this_line.x1,
	  .x2 = this_line.x2,
	  .y = this_line.y1 });

      if (this_line.x1 >= this_line.x2)
      {
	throw std::runtime_error("Error: wrong assumption on svg file format produced by lilypond. x1 not always <= x2");
      }
    }

    auto left   = std::numeric_limits<decltype(rect::left)>::max();
    auto right  = std::numeric_limits<decltype(rect::right)>::min();
    auto top    = std::numeric_limits<decltype(rect::top)>::max();
    auto bottom = std::numeric_limits<decltype(rect::bottom)>::min();

    for (const auto& current_segment : full_line)
    {
      // the svg file always keep x1 <= x2. If not previous check would
      // throw. Obvisously this code would need to be updated. the
      // change would be to: left <- min(left, x1, x2) (and similarly
      // for right)
      left   = std::min(left, current_segment.x1);
      right  = std::max(right, current_segment.x2);
      top    = std::min(top, current_segment.y);
      bottom = std::max(bottom, current_segment.y);
    }

    res.emplace_back(skyline{
	.surface = rect{ .top = top,
			 .bottom = bottom,
			 .left = left,
			 .right = right },
	.full_line = std::move(full_line) });
  }

  // sanity check: (top left corner is (0, 0), so top lines have lower y values
  if (std::any_of(res.begin(), res.end(), [] (const auto& l) {
	return l.surface.top > l.surface.bottom;
      }))
  {
    throw std::runtime_error("Error: inconsistent value found.");
  }


  // sort skyline from top to bottom
  std::sort(res.begin(), res.end(), [] (const auto& a, const auto& b) {
      return (a.surface.top < b.surface.top) or
	((a.surface.top == b.surface.top) and (a.surface.left < b.surface.left));
    });

  return res;
}

#if 0
static inline
std::vector<skyline> get_top_systems_skyline(const pugi::xml_document& svg_file)
{
  return get_skylines(svg_file, "//g[@color=\"rgb(25500.0000%, 0.0000%, 0.0000%)\"]");
}

static inline
std::vector<skyline> get_bottom_systems_skyline(const pugi::xml_document& svg_file)
{
  return get_skylines(svg_file, "//g[@color=\"rgb(0.0000%, 25500.0000%, 0.0000%)\"]");
}
#endif

static inline
std::vector<skyline> get_top_staves_skyline(const pugi::xml_document& svg_file)
{
  return get_skylines(svg_file, "//g[@color=\"rgb(25500.0000%, 0.0000%, 25500.0000%)\"]");
}

static inline
std::vector<skyline> get_bottom_staves_skyline(const pugi::xml_document& svg_file)
{
  return get_skylines(svg_file, "//g[@color=\"rgb(0.0000%, 25500.0000%, 25500.0000%)\"]");
}

static
std::vector<h_segment> filter_segments(const std::vector<h_segment>& vec,
				       uint32_t min_left,
				       uint32_t max_right)
{
  std::vector<h_segment> res;
  res.reserve(vec.size());

  for (const auto& s : vec)
  {
    if ((s.x1 >= min_left) and (s.x2 <= max_right))
    {
      res.emplace_back(s);
    }
  }
  res.shrink_to_fit();
  return res;
}

// precondition svg_file is already parsed
std::vector<staff> get_staves(const pugi::xml_document& svg_file)
{
  const auto staves ( get_staves_surface(svg_file) );
  const auto top_staves ( get_top_staves_skyline(svg_file) );
  const auto bottom_staves ( get_bottom_staves_skyline(svg_file) );

  const auto nb_staves = staves.size();

  // sanity check: each staff must have a bottom skyline.
  if (bottom_staves.size() != nb_staves)
  {
    throw std::runtime_error("Error: mismatch between the top and bottom skylines of staves");
  }

  // sanity check: each staff must have a top skyline.
  if (top_staves.size() != nb_staves)
  {
    throw std::runtime_error("Error: mismatch between the top skylines and staves");
  }

  std::vector<staff> res;
  // since staves, top_skylines, bottom_skylines are all sorted the
  // same way (top to bottom), therefore each skyline in the vector
  // belongs to the staff at the same position
  for (auto i = decltype(nb_staves){0}; i < nb_staves; ++i)
  {
    auto top_line = filter_segments(top_staves[i].full_line,
				    staves[i].left,
				    staves[i].right);

    auto bottom_line = filter_segments(bottom_staves[i].full_line,
				       staves[i].left,
				       staves[i].right);

    const auto max_top_point =  std::min_element(top_line.begin(), top_line.end(), [] (const auto& a, const auto& b) {
	return a.y < b.y;
      });

    const auto min_bottom_point =  std::max_element(bottom_line.begin(), bottom_line.end(), [] (const auto& a, const auto& b) {
	return a.y < b.y;
      });

    if ((max_top_point == top_line.end()) or (min_bottom_point == bottom_line.end()))
    {
      throw std::runtime_error("Error: one skyline is outside the vertical space delimited by the left and right edge of a staff");
    }

    res.emplace_back(staff{
	.x = staves[i].left,
	.y = staves[i].top,
	.width = staves[i].right- staves[i].left,
	.height = staves[i].bottom - staves[i].top,
	.top_skyline = max_top_point->y,
	.bottom_skyline = min_bottom_point->y,
	.full_top_skyline = std::move(top_line),
	.full_bottom_skyline = std::move(bottom_line) });
  }



  return res;
}
