/*
 * Class to fetch/change/send registers to the debugger for x86, x86_64 architectures.
 * Copyright 2013  Vlas Puhov <vlas.puhov@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef REGISTERCONTROLLER_X86_H
#define REGISTERCONTROLLER_X86_H

#include "registercontroller.h"

namespace KDevMI
{
class MIDebugSession;

class RegisterControllerGeneral_x86 : public IRegisterController
{
    Q_OBJECT

public:
    QVector<GroupsName> namesOfRegisterGroups() const override;

public Q_SLOTS:
    void updateRegisters(const GroupsName& group = GroupsName()) override;

protected:
    explicit RegisterControllerGeneral_x86(MIDebugSession* debugSession = nullptr, QObject* parent = nullptr);

    RegistersGroup registersFromGroup(const GroupsName& group) const override;

    QStringList registerNamesForGroup(const GroupsName& group) const override;

    void updateValuesForRegisters(RegistersGroup* registers) const override;

    void setRegisterValueForGroup(const GroupsName& group, const Register& reg) override;

    enum X86RegisterGroups {General, Flags, FPU, XMM, Segment, LAST_REGISTER};

    GroupsName enumToGroupName(X86RegisterGroups group) const;
    //None of functions below checks value for validity, if value is invalid updateRegistres() will restore the previous state.

    void setFPURegister(const Register& reg);
    void setXMMRegister(const Register& reg);
    void setSegmentRegister(const Register& reg);
    void setEFfagRegister(const Register& reg);
private:
    void initRegisterNames();

protected:
    static QVector<QStringList> m_registerNames;

    static FlagRegister m_eflags;

    ///Indicates if register names were initialized.
    bool m_registerNamesInitialized = false;
};

class RegisterController_x86 : public RegisterControllerGeneral_x86
{
    Q_OBJECT

public:
    explicit RegisterController_x86(MIDebugSession* debugSession = nullptr, QObject* parent = nullptr);

private:
    void initRegisterNames();
};

class RegisterController_x86_64 : public RegisterControllerGeneral_x86
{
    Q_OBJECT

public:
    explicit RegisterController_x86_64(MIDebugSession* debugSession = nullptr, QObject* parent = nullptr);

private:
    void initRegisterNames();
};

} // end of namespace KDevMI

#endif // REGISTERCONTROLLER_X86_H
