#include "caret.h"

Caret::Caret()
{
  m_row = 0;
  m_idx = 0;
}

Caret::Caret(const int row, const int idx)
{
  m_row = row;
  m_idx = idx;
}

Caret::Caret(const Caret& value)
{
  m_row = value.m_row;
  m_idx = value.m_idx;
}

bool Caret::operator<(const Caret& compare)
{
  if (m_row<compare.m_row)
    return true;
  else if (m_row==compare.m_row &&
           m_idx<compare.m_idx)
    return true;
  return false;
}

bool Caret::operator>(const Caret& compare)
{
  if (m_row>compare.m_row)
    return true;
  else if (m_row==compare.m_row &&
           m_idx>compare.m_idx)
    return true;
  return false;
}

bool Caret::operator<=(const Caret& compare)
{
  if (*this < compare ||
      *this == compare)
    return true;
  return false;
}

bool Caret::operator>=(const Caret& compare)
{
  if (*this > compare ||
      *this == compare)
    return true;
  return false;
}

bool Caret::operator==(const Caret& compare)
{
  if (m_row==compare.m_row &&
      m_idx==compare.m_idx)
    return true;
  return false;
}

bool Caret::operator!=(const Caret& compare)
{
  if (m_row!=compare.m_row ||
      m_idx!=compare.m_idx)
    return true;
  return false;
}

Caret Caret::operator=(const Caret& value)
{
  
  m_row=value.m_row;
  m_idx=value.m_idx;
  return Caret(m_row,m_idx);
}

Caret Caret::operator+(const Caret& value)
{
  return Caret(m_row+value.m_row,m_idx+value.m_idx);
}

Caret Caret::operator-(const Caret& value)
{
  return Caret(m_row-value.m_row,m_idx-value.m_idx);
}

