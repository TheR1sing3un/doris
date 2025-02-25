// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "runtime/runtime_state.h"
#include "udf/udf_internal.h"
#include "vec/columns/column_nullable.h"
#include "vec/columns/column_string.h"
#include "vec/columns/column_vector.h"
#include "vec/data_types/data_type_date.h"
#include "vec/data_types/data_type_date_time.h"
#include "vec/data_types/data_type_number.h"
#include "vec/data_types/data_type_string.h"
#include "vec/functions/simple_function_factory.h"
#include "vec/runtime/vdatetime_value.h"
#include "vec/utils/util.hpp"

namespace doris::vectorized {

struct StrToDate {
    static constexpr auto name = "str_to_date";

    static Status execute(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                          size_t result, size_t input_rows_count) {
        auto null_map = ColumnUInt8::create(input_rows_count, 0);
        ColumnPtr argument_columns[2];

        // focus convert const to full column to simply execute logic
        // handle
        for (int i = 0; i < 2; ++i) {
            argument_columns[i] =
                    block.get_by_position(arguments[i]).column->convert_to_full_column_if_const();
            if (auto* nullable = check_and_get_column<ColumnNullable>(*argument_columns[i])) {
                // Danger: Here must dispose the null map data first! Because
                // argument_columns[i]=nullable->get_nested_column_ptr(); will release the mem
                // of column nullable mem of null map
                VectorizedUtils::update_null_map(null_map->get_data(),
                                                 nullable->get_null_map_data());
                argument_columns[i] = nullable->get_nested_column_ptr();
            }
        }

        auto specific_str_column = assert_cast<const ColumnString*>(argument_columns[0].get());
        auto specific_char_column = assert_cast<const ColumnString*>(argument_columns[1].get());

        auto& ldata = specific_str_column->get_chars();
        auto& loffsets = specific_str_column->get_offsets();

        auto& rdata = specific_char_column->get_chars();
        auto& roffsets = specific_char_column->get_offsets();

        ColumnPtr res = nullptr;
        WhichDataType which(remove_nullable(block.get_by_position(result).type));
        if (which.is_date_time_v2()) {
            res = ColumnVector<UInt64>::create();
            executeImpl<DateV2Value<DateTimeV2ValueType>, UInt64>(
                    context, ldata, loffsets, rdata, roffsets,
                    static_cast<ColumnVector<UInt64>*>(res->assume_mutable().get())->get_data(),
                    null_map->get_data());
        } else if (which.is_date_v2()) {
            res = ColumnVector<UInt32>::create();
            executeImpl<DateV2Value<DateV2ValueType>, UInt32>(
                    context, ldata, loffsets, rdata, roffsets,
                    static_cast<ColumnVector<UInt32>*>(res->assume_mutable().get())->get_data(),
                    null_map->get_data());
        } else {
            res = ColumnVector<Int64>::create();
            executeImpl<VecDateTimeValue, Int64>(
                    context, ldata, loffsets, rdata, roffsets,
                    static_cast<ColumnVector<Int64>*>(res->assume_mutable().get())->get_data(),
                    null_map->get_data());
        }

        block.get_by_position(result).column =
                ColumnNullable::create(std::move(res), std::move(null_map));
        return Status::OK();
    }

    template <typename DateValueType, typename NativeType>
    static void executeImpl(FunctionContext* context, const ColumnString::Chars& ldata,
                            const ColumnString::Offsets& loffsets, const ColumnString::Chars& rdata,
                            const ColumnString::Offsets& roffsets, PaddedPODArray<NativeType>& res,
                            NullMap& null_map) {
        size_t size = loffsets.size();
        res.resize(size);
        for (size_t i = 0; i < size; ++i) {
            const char* l_raw_str = reinterpret_cast<const char*>(&ldata[loffsets[i - 1]]);
            int l_str_size = loffsets[i] - loffsets[i - 1];

            const char* r_raw_str = reinterpret_cast<const char*>(&rdata[roffsets[i - 1]]);
            int r_str_size = roffsets[i] - roffsets[i - 1];

            auto& ts_val = *reinterpret_cast<DateValueType*>(&res[i]);
            if (!ts_val.from_date_format_str(r_raw_str, r_str_size, l_raw_str, l_str_size)) {
                null_map[i] = 1;
            }
            if constexpr (std::is_same_v<DateValueType, VecDateTimeValue>) {
                if (context->impl()->get_return_type().type ==
                    doris_udf::FunctionContext::Type::TYPE_DATETIME) {
                    ts_val.to_datetime();
                } else {
                    ts_val.cast_to_date();
                }
            }
        }
    }
};

struct MakeDateImpl {
    static constexpr auto name = "makedate";

    static Status execute(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                          size_t result, size_t input_rows_count) {
        auto null_map = ColumnUInt8::create(input_rows_count, 0);
        DCHECK_EQ(arguments.size(), 2);
        ColumnPtr argument_columns[2];
        for (int i = 0; i < 2; ++i) {
            argument_columns[i] =
                    block.get_by_position(arguments[i]).column->convert_to_full_column_if_const();
            if (auto* nullable = check_and_get_column<ColumnNullable>(*argument_columns[i])) {
                // Danger: Here must dispose the null map data first! Because
                // argument_columns[i]=nullable->get_nested_column_ptr(); will release the mem
                // of column nullable mem of null map
                VectorizedUtils::update_null_map(null_map->get_data(),
                                                 nullable->get_null_map_data());
                argument_columns[i] = nullable->get_nested_column_ptr();
            }
        }

        ColumnPtr res = nullptr;
        WhichDataType which(remove_nullable(block.get_by_position(result).type));
        if (which.is_date_v2()) {
            res = ColumnVector<UInt32>::create();
            executeImpl<DateV2Value<DateV2ValueType>, UInt32>(
                    static_cast<const ColumnVector<Int32>*>(argument_columns[0].get())->get_data(),
                    static_cast<const ColumnVector<Int32>*>(argument_columns[1].get())->get_data(),
                    static_cast<ColumnVector<UInt32>*>(res->assume_mutable().get())->get_data(),
                    null_map->get_data());
        } else if (which.is_date_time_v2()) {
            res = ColumnVector<UInt64>::create();
            executeImpl<DateV2Value<DateTimeV2ValueType>, UInt64>(
                    static_cast<const ColumnVector<Int32>*>(argument_columns[0].get())->get_data(),
                    static_cast<const ColumnVector<Int32>*>(argument_columns[1].get())->get_data(),
                    static_cast<ColumnVector<UInt64>*>(res->assume_mutable().get())->get_data(),
                    null_map->get_data());
        } else {
            res = ColumnVector<Int64>::create();
            executeImpl<VecDateTimeValue, Int64>(
                    static_cast<const ColumnVector<Int32>*>(argument_columns[0].get())->get_data(),
                    static_cast<const ColumnVector<Int32>*>(argument_columns[1].get())->get_data(),
                    static_cast<ColumnVector<Int64>*>(res->assume_mutable().get())->get_data(),
                    null_map->get_data());
        }

        block.get_by_position(result).column =
                ColumnNullable::create(std::move(res), std::move(null_map));
        return Status::OK();
    }

    template <typename DateValueType, typename ReturnType>
    static void executeImpl(const PaddedPODArray<Int32>& ldata, const PaddedPODArray<Int32>& rdata,
                            PaddedPODArray<ReturnType>& res, NullMap& null_map) {
        auto len = ldata.size();
        res.resize(len);

        for (size_t i = 0; i < len; ++i) {
            const auto& l = ldata[i];
            const auto& r = rdata[i];
            if (r <= 0 || l < 0 || l > 9999) {
                null_map[i] = 1;
                continue;
            }

            auto& res_val = *reinterpret_cast<DateValueType*>(&res[i]);
            if constexpr (std::is_same_v<DateValueType, VecDateTimeValue>) {
                VecDateTimeValue ts_value = VecDateTimeValue();
                ts_value.set_time(l, 1, 1, 0, 0, 0);

                DateTimeVal ts_val;
                ts_value.to_datetime_val(&ts_val);
                if (ts_val.is_null) {
                    null_map[i] = 1;
                    continue;
                }

                TimeInterval interval(DAY, r - 1, false);
                res_val = VecDateTimeValue::from_datetime_val(ts_val);
                if (!res_val.template date_add_interval<DAY>(interval)) {
                    null_map[i] = 1;
                    continue;
                }
                res_val.cast_to_date();
            } else {
                res_val.set_time(l, 1, 1, 0, 0, 0, 0);
                TimeInterval interval(DAY, r - 1, false);
                if (!res_val.template date_add_interval<DAY>(interval)) {
                    null_map[i] = 1;
                }
            }
        }
    }
};

class FromDays : public IFunction {
public:
    static constexpr auto name = "from_days";

    static FunctionPtr create() { return std::make_shared<FromDays>(); }

    String get_name() const override { return name; }

    bool use_default_implementation_for_constants() const override { return false; }

    size_t get_number_of_arguments() const override { return 1; }

    bool use_default_implementation_for_nulls() const override { return true; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return make_nullable(std::make_shared<DataTypeDate>());
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        size_t result, size_t input_rows_count) override {
        auto null_map = ColumnUInt8::create(input_rows_count, 0);

        ColumnPtr argument_column =
                block.get_by_position(arguments[0]).column->convert_to_full_column_if_const();
        auto data_col = assert_cast<const ColumnVector<Int32>*>(argument_column.get());

        ColumnPtr res_column;
        WhichDataType which(remove_nullable(block.get_by_position(result).type));
        if (which.is_date()) {
            res_column = ColumnInt64::create(input_rows_count);
            execute_straight<VecDateTimeValue, Int64>(
                    input_rows_count, null_map->get_data(), data_col->get_data(),
                    static_cast<ColumnVector<Int64>*>(res_column->assume_mutable().get())
                            ->get_data());
        } else {
            res_column = ColumnVector<UInt32>::create(input_rows_count);
            execute_straight<DateV2Value<DateV2ValueType>, UInt32>(
                    input_rows_count, null_map->get_data(), data_col->get_data(),
                    static_cast<ColumnVector<UInt32>*>(res_column->assume_mutable().get())
                            ->get_data());
        }

        block.replace_by_position(
                result, ColumnNullable::create(std::move(res_column), std::move(null_map)));
        return Status::OK();
    }

private:
    template <typename DateValueType, typename ReturnType>
    void execute_straight(size_t input_rows_count, NullMap& null_map,
                          const PaddedPODArray<Int32>& data_col,
                          PaddedPODArray<ReturnType>& res_data) {
        for (int i = 0; i < input_rows_count; i++) {
            if constexpr (std::is_same_v<DateValueType, VecDateTimeValue>) {
                const auto& cur_data = data_col[i];
                auto& ts_value = *reinterpret_cast<DateValueType*>(&res_data[i]);
                if (!ts_value.from_date_daynr(cur_data)) {
                    null_map[i] = 1;
                    continue;
                }
                DateTimeVal ts_val;
                ts_value.to_datetime_val(&ts_val);
                ts_value = VecDateTimeValue::from_datetime_val(ts_val);
            } else {
                const auto& cur_data = data_col[i];
                auto& ts_value = *reinterpret_cast<DateValueType*>(&res_data[i]);
                if (!ts_value.get_date_from_daynr(cur_data)) {
                    null_map[i] = 1;
                }
            }
        }
    }
};

struct UnixTimeStampImpl {
    static Int32 trim_timestamp(Int64 timestamp) {
        if (timestamp < 0 || timestamp > INT_MAX) {
            timestamp = 0;
        }
        return (Int32)timestamp;
    }

    static DataTypes get_variadic_argument_types() { return {}; }

    static DataTypePtr get_return_type_impl(const ColumnsWithTypeAndName& arguments) {
        return std::make_shared<DataTypeInt32>();
    }

    static Status execute_impl(FunctionContext* context, Block& block,
                               const ColumnNumbers& arguments, size_t result,
                               size_t input_rows_count) {
        auto col_result = ColumnVector<Int32>::create();
        col_result->resize(input_rows_count);
        // TODO: use a const column to store this value
        auto& col_result_data = col_result->get_data();
        auto res_value = context->impl()->state()->timestamp_ms() / 1000;
        for (int i = 0; i < input_rows_count; i++) {
            col_result_data[i] = res_value;
        }
        block.replace_by_position(result, std::move(col_result));
        return Status::OK();
    }
};

template <typename DateType>
struct UnixTimeStampDateImpl {
    static DataTypes get_variadic_argument_types() { return {std::make_shared<DateType>()}; }

    static DataTypePtr get_return_type_impl(const ColumnsWithTypeAndName& arguments) {
        return make_nullable(std::make_shared<DataTypeInt32>());
    }

    static Status execute_impl(FunctionContext* context, Block& block,
                               const ColumnNumbers& arguments, size_t result,
                               size_t input_rows_count) {
        const ColumnPtr col_source = block.get_by_position(arguments[0]).column;

        auto col_result = ColumnVector<Int32>::create();
        auto null_map = ColumnVector<UInt8>::create();

        col_result->resize(input_rows_count);
        null_map->resize(input_rows_count);

        auto& col_result_data = col_result->get_data();
        auto& null_map_data = null_map->get_data();

        for (int i = 0; i < input_rows_count; i++) {
            if (col_source->is_null_at(i)) {
                null_map_data[i] = true;
                continue;
            }

            StringRef source = col_source->get_data_at(i);
            if constexpr (std::is_same_v<DateType, DataTypeDate>) {
                const VecDateTimeValue& ts_value =
                        reinterpret_cast<const VecDateTimeValue&>(*source.data);
                int64_t timestamp;
                if (!ts_value.unix_timestamp(&timestamp,
                                             context->impl()->state()->timezone_obj())) {
                    null_map_data[i] = true;
                } else {
                    null_map_data[i] = false;
                    col_result_data[i] = UnixTimeStampImpl::trim_timestamp(timestamp);
                }
            } else if constexpr (std::is_same_v<DateType, DataTypeDateV2>) {
                const DateV2Value<DateV2ValueType>& ts_value =
                        reinterpret_cast<const DateV2Value<DateV2ValueType>&>(*source.data);
                int64_t timestamp;
                if (!ts_value.unix_timestamp(&timestamp,
                                             context->impl()->state()->timezone_obj())) {
                    null_map_data[i] = true;
                } else {
                    null_map_data[i] = false;
                    col_result_data[i] = UnixTimeStampImpl::trim_timestamp(timestamp);
                }
            } else {
                const DateV2Value<DateTimeV2ValueType>& ts_value =
                        reinterpret_cast<const DateV2Value<DateTimeV2ValueType>&>(*source.data);
                int64_t timestamp;
                if (!ts_value.unix_timestamp(&timestamp,
                                             context->impl()->state()->timezone_obj())) {
                    null_map_data[i] = true;
                } else {
                    null_map_data[i] = false;
                    col_result_data[i] = UnixTimeStampImpl::trim_timestamp(timestamp);
                }
            }
        }

        block.replace_by_position(
                result, ColumnNullable::create(std::move(col_result), std::move(null_map)));

        return Status::OK();
    }
};

template <typename DateType>
struct UnixTimeStampDatetimeImpl : public UnixTimeStampDateImpl<DateType> {
    static DataTypes get_variadic_argument_types() { return {std::make_shared<DateType>()}; }
};

struct UnixTimeStampStrImpl {
    static DataTypes get_variadic_argument_types() {
        return {std::make_shared<DataTypeString>(), std::make_shared<DataTypeString>()};
    }

    static DataTypePtr get_return_type_impl(const ColumnsWithTypeAndName& arguments) {
        return make_nullable(std::make_shared<DataTypeInt32>());
    }

    static Status execute_impl(FunctionContext* context, Block& block,
                               const ColumnNumbers& arguments, size_t result,
                               size_t input_rows_count) {
        const ColumnPtr col_source = block.get_by_position(arguments[0]).column;
        const ColumnPtr col_format = block.get_by_position(arguments[1]).column;

        auto col_result = ColumnVector<Int32>::create();
        auto null_map = ColumnVector<UInt8>::create();

        col_result->resize(input_rows_count);
        null_map->resize(input_rows_count);

        auto& col_result_data = col_result->get_data();
        auto& null_map_data = null_map->get_data();

        for (int i = 0; i < input_rows_count; i++) {
            if (col_source->is_null_at(i) || col_format->is_null_at(i)) {
                null_map_data[i] = true;
                continue;
            }

            StringRef source = col_source->get_data_at(i);
            StringRef fmt = col_format->get_data_at(i);

            VecDateTimeValue ts_value;
            if (!ts_value.from_date_format_str(fmt.data, fmt.size, source.data, source.size)) {
                null_map_data[i] = true;
                continue;
            }

            int64_t timestamp;
            if (!ts_value.unix_timestamp(&timestamp, context->impl()->state()->timezone_obj())) {
                null_map_data[i] = true;
            } else {
                null_map_data[i] = false;
                col_result_data[i] = UnixTimeStampImpl::trim_timestamp(timestamp);
            }
        }

        block.replace_by_position(
                result, ColumnNullable::create(std::move(col_result), std::move(null_map)));

        return Status::OK();
    }
};

template <typename Impl>
class FunctionUnixTimestamp : public IFunction {
public:
    static constexpr auto name = "unix_timestamp";
    static FunctionPtr create() { return std::make_shared<FunctionUnixTimestamp<Impl>>(); }

    String get_name() const override { return name; }

    bool use_default_implementation_for_nulls() const override { return false; }

    size_t get_number_of_arguments() const override {
        return get_variadic_argument_types_impl().size();
    }

    DataTypePtr get_return_type_impl(const ColumnsWithTypeAndName& arguments) const override {
        return Impl::get_return_type_impl(arguments);
    }

    DataTypes get_variadic_argument_types_impl() const override {
        return Impl::get_variadic_argument_types();
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        size_t result, size_t input_rows_count) override {
        return Impl::execute_impl(context, block, arguments, result, input_rows_count);
    }
};

template <typename Impl>
class FunctionOtherTypesToDateType : public IFunction {
public:
    static constexpr auto name = Impl::name;
    static FunctionPtr create() { return std::make_shared<FunctionOtherTypesToDateType>(); }

    String get_name() const override { return name; }

    size_t get_number_of_arguments() const override { return 2; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return make_nullable(std::make_shared<DataTypeDateTime>());
    }

    bool use_default_implementation_for_constants() const override { return true; }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        size_t result, size_t input_rows_count) override {
        return Impl::execute(context, block, arguments, result, input_rows_count);
    }
};

using FunctionStrToDate = FunctionOtherTypesToDateType<StrToDate>;

using FunctionMakeDate = FunctionOtherTypesToDateType<MakeDateImpl>;

void register_function_timestamp(SimpleFunctionFactory& factory) {
    factory.register_function<FunctionStrToDate>();
    factory.register_function<FunctionMakeDate>();
    factory.register_function<FromDays>();

    factory.register_function<FunctionUnixTimestamp<UnixTimeStampImpl>>();
    factory.register_function<FunctionUnixTimestamp<UnixTimeStampDateImpl<DataTypeDate>>>();
    factory.register_function<FunctionUnixTimestamp<UnixTimeStampDateImpl<DataTypeDateV2>>>();
    factory.register_function<FunctionUnixTimestamp<UnixTimeStampDateImpl<DataTypeDateTimeV2>>>();
    factory.register_function<FunctionUnixTimestamp<UnixTimeStampDatetimeImpl<DataTypeDate>>>();
    factory.register_function<FunctionUnixTimestamp<UnixTimeStampDatetimeImpl<DataTypeDateV2>>>();
    factory.register_function<
            FunctionUnixTimestamp<UnixTimeStampDatetimeImpl<DataTypeDateTimeV2>>>();
    factory.register_function<FunctionUnixTimestamp<UnixTimeStampStrImpl>>();
}

} // namespace doris::vectorized
