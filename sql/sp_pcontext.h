/* -*- C++ -*- */
/* Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef _SP_PCONTEXT_H_
#define _SP_PCONTEXT_H_

#ifdef USE_PRAGMA_INTERFACE
#pragma interface			/* gcc class implementation */
#endif

#include "sql_string.h"                         // LEX_STRING
#include "mysql_com.h"                          // enum_field_types
#include "field.h"                              // Create_field
#include "sql_array.h"                          // Dynamic_array


/// This class represents a stored program variable or a parameter
/// (also referenced as 'SP-variable').

class sp_variable : public Sql_alloc
{
public:
  enum enum_mode
  {
    MODE_IN,
    MODE_OUT,
    MODE_INOUT
  };

  /// Name of the SP-variable.
  LEX_CSTRING name;

  /// Mode of the SP-variable.
  enum_mode mode;

  /// The index to the variable's value in the runtime frame.
  ///
  /// It is calculated during parsing and used when creating sp_instr_set
  /// instructions and Item_splocal items. I.e. values are set/referred by
  /// array indexing in runtime.
  uint offset;

  /// Default value of the SP-variable (if any).
  Item *default_value;

  /// Full type information (field meta-data) of the SP-variable.
  Spvar_definition field_def;

  /// Field-type of the SP-variable.
  const Type_handler *type_handler() const { return field_def.type_handler(); }

public:
  sp_variable(const LEX_CSTRING *name_arg, uint offset_arg)
   :Sql_alloc(),
    name(*name_arg),
    mode(MODE_IN),
    offset(offset_arg),
    default_value(NULL)
  { }
  /*
    Find a ROW field by its qualified name.
    @param      var_name - the name of the variable
    @param      field_name - the name of the variable field
    @param[OUT] row_field_offset - the index of the field

    @retval  NULL if the variable with the given name was not found,
             or it is not a row variable, or it does not have a field
             with the given name, or a non-null pointer otherwise.
             row_field_offset[0] is set only when the method returns !NULL.
  */
  const Spvar_definition *find_row_field(const LEX_CSTRING *var_name,
                                         const LEX_CSTRING *field_name,
                                         uint *row_field_offset);
};

///////////////////////////////////////////////////////////////////////////

/// This class represents an SQL/PSM label. Can refer to the identifier
/// used with the "label_name:" construct which may precede some SQL/PSM
/// statements, or to an implicit implementation-dependent identifier which
/// the parser inserts before a high-level flow control statement such as
/// IF/WHILE/REPEAT/LOOP, when such statement is rewritten into a
/// combination of low-level jump/jump_if instructions and labels.


class sp_label : public Sql_alloc
{
public:
  enum enum_type
  {
    /// Implicit label generated by parser.
    IMPLICIT,

    /// Label at BEGIN.
    BEGIN,

    /// Label at iteration control
    ITERATION,

    /// Label for jump
    GOTO
  };

  /// Name of the label.
  LEX_CSTRING name;

  /// Instruction pointer of the label.
  uint ip;

  /// Type of the label.
  enum_type type;

  /// Scope of the label.
  class sp_pcontext *ctx;

public:
  sp_label(const LEX_CSTRING *_name,
           uint _ip, enum_type _type, sp_pcontext *_ctx)
   :Sql_alloc(),
    name(*_name),
    ip(_ip),
    type(_type),
    ctx(_ctx)
  { }
};


///////////////////////////////////////////////////////////////////////////

/// This class represents condition-value term in DECLARE CONDITION or
/// DECLARE HANDLER statements. sp_condition_value has little to do with
/// SQL-conditions.
///
/// In some sense, this class is a union -- a set of filled attributes
/// depends on the sp_condition_value::type value.

class sp_condition_value : public Sql_alloc, public Sql_state_errno
{
  bool m_is_user_defined;
public:
  enum enum_type
  {
    ERROR_CODE,
    SQLSTATE,
    WARNING,
    NOT_FOUND,
    EXCEPTION
  };

  /// Type of the condition value.
  enum_type type;

public:
  sp_condition_value(uint _mysqlerr)
   :Sql_alloc(),
    Sql_state_errno(_mysqlerr),
    m_is_user_defined(false),
    type(ERROR_CODE)
  { }

  sp_condition_value(uint _mysqlerr, const char *_sql_state)
   :Sql_alloc(),
    Sql_state_errno(_mysqlerr, _sql_state),
    m_is_user_defined(false),
    type(ERROR_CODE)
  { }

  sp_condition_value(const char *_sql_state, bool is_user_defined= false)
   :Sql_alloc(),
    Sql_state_errno(0, _sql_state),
    m_is_user_defined(is_user_defined),
    type(SQLSTATE)
  { }

  sp_condition_value(enum_type _type)
   :Sql_alloc(),
    m_is_user_defined(false),
    type(_type)
  {
    DBUG_ASSERT(type != ERROR_CODE && type != SQLSTATE);
  }

  /// Check if two instances of sp_condition_value are equal or not.
  ///
  /// @param cv another instance of sp_condition_value to check.
  ///
  /// @return true if the instances are equal, false otherwise.
  bool equals(const sp_condition_value *cv) const;


  /**
    Checks if this condition is OK for search.
    See also sp_context::find_handler().

    @param identity - The condition identity
    @param found_cv - A previously found matching condition or NULL.
    @return true    - If the current value matches identity and
                      makes a stronger match than the previously
                      found condition found_cv.
    @return false   - If the current value does not match identity,
                      of the current value makes a weaker match than found_cv.
  */
  bool matches(const Sql_condition_identity &identity,
               const sp_condition_value *found_cv) const;

  Sql_user_condition_identity get_user_condition_identity() const
  {
    return Sql_user_condition_identity(m_is_user_defined ? this : NULL);
  }
};


class sp_condition_value_user_defined: public sp_condition_value
{
public:
  sp_condition_value_user_defined()
   :sp_condition_value("45000", true)
  { }
};


///////////////////////////////////////////////////////////////////////////

/// This class represents 'DECLARE CONDITION' statement.
/// sp_condition has little to do with SQL-conditions.

class sp_condition : public Sql_alloc
{
public:
  /// Name of the condition.
  LEX_CSTRING name;

  /// Value of the condition.
  sp_condition_value *value;

public:
  sp_condition(const LEX_CSTRING *name_arg, sp_condition_value *value_arg)
   :Sql_alloc(),
    name(*name_arg),
    value(value_arg)
  { }
  sp_condition(const char *name_arg, size_t name_length_arg,
               sp_condition_value *value_arg)
   :value(value_arg)
  {
    name.str=    name_arg;
    name.length= name_length_arg;
  }
  bool eq_name(const LEX_CSTRING *str) const
  {
    return my_strnncoll(system_charset_info,
                        (const uchar *) name.str, name.length,
                        (const uchar *) str->str, str->length) == 0;
  }
};


///////////////////////////////////////////////////////////////////////////

/**
  class sp_pcursor.
  Stores information about a cursor:
  - Cursor's name in LEX_STRING.
  - Cursor's formal parameter descriptions.

    Formal parameter descriptions reside in a separate context block,
    pointed by the "m_param_context" member.

    m_param_context can be NULL. This means a cursor with no parameters.
    Otherwise, the number of variables in m_param_context means
    the number of cursor's formal parameters.

    Note, m_param_context can be not NULL, but have no variables.
    This is also means a cursor with no parameters (similar to NULL).
*/
class sp_pcursor: public LEX_CSTRING
{
  class sp_pcontext *m_param_context; // Formal parameters
  class sp_lex_cursor *m_lex;         // The cursor statement LEX
public:
  sp_pcursor(const LEX_CSTRING *name, class sp_pcontext *param_ctx,
             class sp_lex_cursor *lex)
   :LEX_CSTRING(*name), m_param_context(param_ctx), m_lex(lex)
  { }
  class sp_pcontext *param_context() const { return m_param_context; }
  class sp_lex_cursor *lex() const { return m_lex; }
  bool check_param_count_with_error(uint param_count) const;
};


///////////////////////////////////////////////////////////////////////////

/// This class represents 'DECLARE HANDLER' statement.

class sp_handler : public Sql_alloc
{
public:
  /// Enumeration of possible handler types.
  /// Note: UNDO handlers are not (and have never been) supported.
  enum enum_type
  {
    EXIT,
    CONTINUE
  };

  /// Handler type.
  enum_type type;

  /// Conditions caught by this handler.
  List<sp_condition_value> condition_values;

public:
  /// The constructor.
  ///
  /// @param _type SQL-handler type.
  sp_handler(enum_type _type)
   :Sql_alloc(),
    type(_type)
  { }
};

///////////////////////////////////////////////////////////////////////////

/// The class represents parse-time context, which keeps track of declared
/// variables/parameters, conditions, handlers, cursors and labels.
///
/// sp_pcontext objects are organized in a tree according to the following
/// rules:
///   - one sp_pcontext object corresponds for for each BEGIN..END block;
///   - one sp_pcontext object corresponds for each exception handler;
///   - one additional sp_pcontext object is created to contain
///     Stored Program parameters.
///
/// sp_pcontext objects are used both at parse-time and at runtime.
///
/// During the parsing stage sp_pcontext objects are used:
///   - to look up defined names (e.g. declared variables and visible
///     labels);
///   - to check for duplicates;
///   - for error checking;
///   - to calculate offsets to be used at runtime.
///
/// During the runtime phase, a tree of sp_pcontext objects is used:
///   - for error checking (e.g. to check correct number of parameters);
///   - to resolve SQL-handlers.

class sp_pcontext : public Sql_alloc
{
public:
  enum enum_scope
  {
    /// REGULAR_SCOPE designates regular BEGIN ... END blocks.
    REGULAR_SCOPE,

    /// HANDLER_SCOPE designates SQL-handler blocks.
    HANDLER_SCOPE
  };

  class Lex_for_loop: public Lex_for_loop_st
  {
  public:
    Lex_for_loop() { init(); }
  };

public:
  sp_pcontext();
  ~sp_pcontext();


  /// Create and push a new context in the tree.

  /// @param thd   thread context.
  /// @param scope scope of the new parsing context.
  /// @return the node created.
  sp_pcontext *push_context(THD *thd, enum_scope scope);

  /// Pop a node from the parsing context tree.
  /// @return the parent node.
  sp_pcontext *pop_context();

  sp_pcontext *parent_context() const
  { return m_parent; }

  /// Calculate and return the number of handlers to pop between the given
  /// context and this one.
  ///
  /// @param ctx       the other parsing context.
  /// @param exclusive specifies if the last scope should be excluded.
  ///
  /// @return the number of handlers to pop between the given context and
  /// this one.  If 'exclusive' is true, don't count the last scope we are
  /// leaving; this is used for LEAVE where we will jump to the hpop
  /// instructions.
  uint diff_handlers(const sp_pcontext *ctx, bool exclusive) const;

  /// Calculate and return the number of cursors to pop between the given
  /// context and this one.
  ///
  /// @param ctx       the other parsing context.
  /// @param exclusive specifies if the last scope should be excluded.
  ///
  /// @return the number of cursors to pop between the given context and
  /// this one.  If 'exclusive' is true, don't count the last scope we are
  /// leaving; this is used for LEAVE where we will jump to the cpop
  /// instructions.
  uint diff_cursors(const sp_pcontext *ctx, bool exclusive) const;

  /////////////////////////////////////////////////////////////////////////
  // SP-variables (parameters and variables).
  /////////////////////////////////////////////////////////////////////////

  /// @return the maximum number of variables used in this and all child
  /// contexts. For the root parsing context, this gives us the number of
  /// slots needed for variables during the runtime phase.
  uint max_var_index() const
  { return m_max_var_index; }

  /// @return the current number of variables used in the parent contexts
  /// (from the root), including this context.
  uint current_var_count() const
  { return m_var_offset + (uint)m_vars.elements(); }

  /// @return the number of variables in this context alone.
  uint context_var_count() const
  { return (uint)m_vars.elements(); }

  /// return the i-th variable on the current context
  sp_variable *get_context_variable(uint i) const
  {
    DBUG_ASSERT(i < m_vars.elements());
    return m_vars.at(i);
  }

  /*
    Return the i-th last context variable.
    If i is 0, then return the very last variable in m_vars.
  */
  sp_variable *get_last_context_variable(uint i= 0) const
  {
    DBUG_ASSERT(i < m_vars.elements());
    return m_vars.at(m_vars.elements() - i - 1);
  }

  /// Add SP-variable to the parsing context.
  ///
  /// @param thd  Thread context.
  /// @param name Name of the SP-variable.
  ///
  /// @return instance of newly added SP-variable.
  sp_variable *add_variable(THD *thd, const LEX_CSTRING *name);

  /// Retrieve full type information about SP-variables in this parsing
  /// context and its children.
  ///
  /// @param field_def_lst[out] Container to store type information.
  void retrieve_field_definitions(List<Spvar_definition> *field_def_lst) const;

  /// Find SP-variable by name.
  ///
  /// The function does a linear search (from newer to older variables,
  /// in case we have shadowed names).
  ///
  /// The function is called only at parsing time.
  ///
  /// @param name               Variable name.
  /// @param current_scope_only A flag if we search only in current scope.
  ///
  /// @return instance of found SP-variable, or NULL if not found.
  sp_variable *find_variable(const LEX_CSTRING *name, bool current_scope_only) const;

  /// Find SP-variable by the offset in the root parsing context.
  ///
  /// The function is used for two things:
  /// - When evaluating parameters at the beginning, and setting out parameters
  ///   at the end, of invocation. (Top frame only, so no recursion then.)
  /// - For printing of sp_instr_set. (Debug mode only.)
  ///
  /// @param offset Variable offset in the root parsing context.
  ///
  /// @return instance of found SP-variable, or NULL if not found.
  sp_variable *find_variable(uint offset) const;

  /// Set the current scope boundary (for default values).
  ///
  /// @param n The number of variables to skip.
  void declare_var_boundary(uint n)
  { m_pboundary= n; }

  /////////////////////////////////////////////////////////////////////////
  // CASE expressions.
  /////////////////////////////////////////////////////////////////////////

  int register_case_expr()
  { return m_num_case_exprs++; }

  int get_num_case_exprs() const
  { return m_num_case_exprs; }

  bool push_case_expr_id(int case_expr_id)
  { return m_case_expr_ids.append(case_expr_id); }

  void pop_case_expr_id()
  { m_case_expr_ids.pop(); }

  int get_current_case_expr_id() const
  { return *m_case_expr_ids.back(); }

  /////////////////////////////////////////////////////////////////////////
  // Labels.
  /////////////////////////////////////////////////////////////////////////

  sp_label *push_label(THD *thd, const LEX_CSTRING *name, uint ip,
                       sp_label::enum_type type, List<sp_label> * list);

  sp_label *push_label(THD *thd, const LEX_CSTRING *name, uint ip,
                       sp_label::enum_type type)
  { return push_label(thd, name, ip, type, &m_labels); }

  sp_label *push_goto_label(THD *thd, const LEX_CSTRING *name, uint ip,
                            sp_label::enum_type type)
  { return push_label(thd, name, ip, type, &m_goto_labels); }

  sp_label *push_label(THD *thd, const LEX_CSTRING *name, uint ip)
  { return push_label(thd, name, ip, sp_label::IMPLICIT); }

  sp_label *push_goto_label(THD *thd, const LEX_CSTRING *name, uint ip)
  { return push_goto_label(thd, name, ip, sp_label::GOTO); }

  sp_label *find_label(const LEX_CSTRING *name);

  sp_label *find_goto_label(const LEX_CSTRING *name, bool recusive);

  sp_label *find_goto_label(const LEX_CSTRING *name)
  { return find_goto_label(name, true); }

  sp_label *find_label_current_loop_start();

  sp_label *last_label()
  {
    sp_label *label= m_labels.head();

    if (!label && m_parent)
      label= m_parent->last_label();

    return label;
  }

  sp_label *last_goto_label()
  {
    return m_goto_labels.head();
  }

  sp_label *pop_label()
  { return m_labels.pop(); }

  bool block_label_declare(LEX_CSTRING *label)
  {
    sp_label *lab= find_label(label);
    if (lab)
    {
      my_error(ER_SP_LABEL_REDEFINE, MYF(0), label->str);
      return true;
    }
    return false;
  }

  /////////////////////////////////////////////////////////////////////////
  // Conditions.
  /////////////////////////////////////////////////////////////////////////

  bool add_condition(THD *thd, const LEX_CSTRING *name,
                               sp_condition_value *value);

  /// See comment for find_variable() above.
  sp_condition_value *find_condition(const LEX_CSTRING *name,
                                     bool current_scope_only) const;

  sp_condition_value *
  find_declared_or_predefined_condition(const LEX_CSTRING *name) const
  {
    sp_condition_value *p= find_condition(name, false);
    if (p)
      return p;
    return find_predefined_condition(name);
  }

  bool declare_condition(THD *thd, const LEX_CSTRING *name,
                                   sp_condition_value *val)
  {
    if (find_condition(name, true))
    {
      my_error(ER_SP_DUP_COND, MYF(0), name->str);
      return true;
    }
    return add_condition(thd, name, val);
  }

  /////////////////////////////////////////////////////////////////////////
  // Handlers.
  /////////////////////////////////////////////////////////////////////////

  sp_handler *add_handler(THD* thd, sp_handler::enum_type type);

  /// This is an auxilary parsing-time function to check if an SQL-handler
  /// exists in the current parsing context (current scope) for the given
  /// SQL-condition. This function is used to check for duplicates during
  /// the parsing phase.
  ///
  /// This function can not be used during the runtime phase to check
  /// SQL-handler existence because it searches for the SQL-handler in the
  /// current scope only (during runtime, current and parent scopes
  /// should be checked according to the SQL-handler resolution rules).
  ///
  /// @param condition_value the handler condition value
  ///                        (not SQL-condition!).
  ///
  /// @retval true if such SQL-handler exists.
  /// @retval false otherwise.
  bool check_duplicate_handler(const sp_condition_value *cond_value) const;

  /// Find an SQL handler for the given SQL condition according to the
  /// SQL-handler resolution rules. This function is used at runtime.
  ///
  /// @param value            The error code and the SQL state
  /// @param level            The SQL condition level
  ///
  /// @return a pointer to the found SQL-handler or NULL.
  sp_handler *find_handler(const Sql_condition_identity &identity) const;

  /////////////////////////////////////////////////////////////////////////
  // Cursors.
  /////////////////////////////////////////////////////////////////////////

  bool add_cursor(const LEX_CSTRING *name, sp_pcontext *param_ctx,
                  class sp_lex_cursor *lex);

  /// See comment for find_variable() above.
  const sp_pcursor *find_cursor(const LEX_CSTRING *name,
                                uint *poff, bool current_scope_only) const;

  const sp_pcursor *find_cursor_with_error(const LEX_CSTRING *name,
                                           uint *poff,
                                           bool current_scope_only) const
  {
    const sp_pcursor *pcursor= find_cursor(name, poff, current_scope_only);
    if (!pcursor)
    {
      my_error(ER_SP_CURSOR_MISMATCH, MYF(0), name->str);
      return NULL;
    }
    return pcursor;
  }
  /// Find cursor by offset (for SHOW {PROCEDURE|FUNCTION} CODE only).
  const sp_pcursor *find_cursor(uint offset) const;

  const sp_pcursor *get_cursor_by_local_frame_offset(uint offset) const
  { return &m_cursors.at(offset); }

  uint cursor_offset() const
  { return m_cursor_offset; }

  uint frame_cursor_count() const
  { return (uint)m_cursors.elements(); }

  uint max_cursor_index() const
  { return m_max_cursor_index + (uint)m_cursors.elements(); }

  uint current_cursor_count() const
  { return m_cursor_offset + (uint)m_cursors.elements(); }

  void set_for_loop(const Lex_for_loop_st &for_loop)
  {
    m_for_loop.init(for_loop);
  }
  const Lex_for_loop_st &for_loop()
  {
    return m_for_loop;
  }

private:
  /// Constructor for a tree node.
  /// @param prev the parent parsing context
  /// @param scope scope of this parsing context
  sp_pcontext(sp_pcontext *prev, enum_scope scope);

  void init(uint var_offset, uint cursor_offset, int num_case_expressions);

  /* Prevent use of these */
  sp_pcontext(const sp_pcontext &);
  void operator=(sp_pcontext &);

  sp_condition_value *find_predefined_condition(const LEX_CSTRING *name) const;

private:
  /// m_max_var_index -- number of variables (including all types of arguments)
  /// in this context including all children contexts.
  ///
  /// m_max_var_index >= m_vars.elements().
  ///
  /// m_max_var_index of the root parsing context contains number of all
  /// variables (including arguments) in all enclosed contexts.
  uint m_max_var_index;

  /// The maximum sub context's framesizes.
  uint m_max_cursor_index;

  /// Parent context.
  sp_pcontext *m_parent;

  /// An index of the first SP-variable in this parsing context. The index
  /// belongs to a runtime table of SP-variables.
  ///
  /// Note:
  ///   - m_var_offset is 0 for root parsing context;
  ///   - m_var_offset is different for all nested parsing contexts.
  uint m_var_offset;

  /// Cursor offset for this context.
  uint m_cursor_offset;

  /// Boundary for finding variables in this context. This is the number of
  /// variables currently "invisible" to default clauses. This is normally 0,
  /// but will be larger during parsing of DECLARE ... DEFAULT, to get the
  /// scope right for DEFAULT values.
  uint m_pboundary;

  int m_num_case_exprs;

  /// SP parameters/variables.
  Dynamic_array<sp_variable *> m_vars;

  /// Stack of CASE expression ids.
  Dynamic_array<int> m_case_expr_ids;

  /// Stack of SQL-conditions.
  Dynamic_array<sp_condition *> m_conditions;

  /// Stack of cursors.
  Dynamic_array<sp_pcursor> m_cursors;

  /// Stack of SQL-handlers.
  Dynamic_array<sp_handler *> m_handlers;

  /*
   In the below example the label <<lab>> has two meanings:
   - GOTO lab : must go before the beginning of the loop
   - CONTINUE lab : must go to the beginning of the loop
   We solve this by storing block labels and goto labels into separate lists.

   BEGIN
     <<lab>>
     FOR i IN a..10 LOOP
       ...
       GOTO lab;
       ...
       CONTINUE lab;
       ...
     END LOOP;
   END;
  */
  /// List of block labels
  List<sp_label> m_labels;
  /// List of goto labels
  List<sp_label> m_goto_labels;

  /// Children contexts, used for destruction.
  Dynamic_array<sp_pcontext *> m_children;

  /// Scope of this parsing context.
  enum_scope m_scope;

  /// FOR LOOP characteristics
  Lex_for_loop m_for_loop;
}; // class sp_pcontext : public Sql_alloc


#endif /* _SP_PCONTEXT_H_ */
