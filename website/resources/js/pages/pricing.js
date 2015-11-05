$(document).ready(function() {

  var users = $('.estimation table .users td span');
  var usersLabel = $('#usersLabel span');
  var services = $('.estimation table tr th');
  var capacityLabel = $('#capacityLabel span');
  var prices = $('.estimation table .price td span');

  // Price Update
  function updatePrice() {
    $.each(prices, function(key, element) {
      var service = $(element).parent().attr('data-service');
      var el = $(users).parent().filter("[data-service='" + service + "']");
      var current_users = parseInt($(usersLabel).text());
      var price_per_user = $(element).attr('data-price');
      var price_per_gb = $(element).attr('data-price-gb');
      var free_until = parseInt($(element).attr('data-free-until'));
      var start_at = parseInt($(element).attr('data-start-at'));
      var new_price = 0;

      if (service === 'egnyte' && current_users > 24) {
        price_per_user = $(element).attr('data-price-max');
      }

      new_price =  price_per_user * 12 * current_users;

      if (free_until) {
        if (current_users >= free_until) {
          new_price = new_price + ($(capacityLabel).text() * price_per_gb * 2);
        } else {
          new_price = $(capacityLabel).text() * price_per_gb * 2;
        }
      }

      $(element).text(new_price);
    });
  }

  // Users Slider
  $("#usersInput").bind("slider:changed", function (event, data) {
    usersLabel.text(data.value);

    $.each(services, function(key, element) {
      var service = $(element).attr('data-service');

      if ($(element).attr('data-start-at')) {
        if ($(element).attr('data-start-at') > data.value) {
          $("[data-service='" + service + "']").addClass('users_disabled');
        } else {
          $("[data-service='" + service + "']").removeClass('users_disabled');
        }
      }
    });

    updatePrice();
  });

  // Capacity Slider
  $("#capacityInput").bind("slider:changed", function (event, data) {

    $.each(services, function(key, element) {
      var service = $(element).attr('data-service');

      if ($(element).attr('data-max-capacity')) {
        if ($(element).attr('data-max-capacity') < data.value) {
          $("[data-service='" + service + "']").addClass('capacity_disabled');
        } else {
          $("[data-service='" + service + "']").removeClass('capacity_disabled');
        }
      }
    });

    capacityLabel.text(data.value);
    updatePrice();
  });

  updatePrice();

});