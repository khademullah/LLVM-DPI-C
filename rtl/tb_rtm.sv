import "DPI-C" function void dpi_reset(input int domains);
import "DPI-C" function void dpi_begin(input int domain, input int is_write, input int data);
import "DPI-C" function void dpi_check(input int head, input int data, input int is_write, input int domain);
import "DPI-C" function int dpi_failures();

module tb_rtm;
    localparam int DOMAINS = 64;

    logic        clk = 0;
    logic        rst_n;
    logic        cmd_valid;
    logic        cmd_write;
    logic [15:0] cmd_domain;
    logic [31:0] cmd_wdata;
    logic        cmd_ready;
    logic        done;
    logic [31:0] rdata;
    logic [15:0] head;
    logic        fault;

    rtm_ctrl #(.DOMAINS(DOMAINS)) dut (
        .clk(clk),
        .rst_n(rst_n),
        .cmd_valid(cmd_valid),
        .cmd_write(cmd_write),
        .cmd_domain(cmd_domain),
        .cmd_wdata(cmd_wdata),
        .cmd_ready(cmd_ready),
        .done(done),
        .rdata(rdata),
        .head(head),
        .fault(fault)
    );

    /* verilator lint_off BLKSEQ */
    always #5 clk = ~clk;
    /* verilator lint_on BLKSEQ */

    task automatic issue(input int domain, input bit wr, input int data);
        dpi_begin(domain, int'(wr), data);
        cmd_domain = domain[15:0];
        cmd_write  = wr;
        cmd_wdata  = data;
        cmd_valid  = 1'b1;
        @(posedge clk);
        cmd_valid = 1'b0;
        @(posedge clk iff done);
        if (!cmd_ready || fault)
            $fatal(1, "controller not idle after command");
        dpi_check(int'(head), int'(rdata), int'(wr), domain);
    endtask

    initial begin
        rst_n     = 0;
        cmd_valid = 0;
        cmd_write = 0;
        cmd_domain = '0;
        cmd_wdata = '0;
        dpi_reset(DOMAINS);
        repeat (2) @(posedge clk);
        rst_n = 1;
        @(posedge clk);

        // Words are dense in the low bits so they are legal on the nanowire.
        issue(0, 1, 32'hF0F0F0F1);
        issue(3, 1, 32'hF0F0F0F2);
        issue(4, 1, 32'hF0F0F0F3);
        issue(1, 1, 32'hF0F0F0F4);
        issue(0, 0, 0);
        issue(4, 0, 0);
        issue(3, 0, 0);
        issue(1, 0, 0);

        if (dpi_failures() != 0) begin
            $display("DPI-C co-sim: FAIL (%0d mismatches)", dpi_failures());
            $fatal(1);
        end
        $display("DPI-C co-sim: PASS");
        $finish;
    end
endmodule
